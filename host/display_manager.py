import subprocess
import re
import logging

logger = logging.getLogger(__name__)

class DisplayManager:
    def __init__(self):
        self.original_mode = None
        self.output_name = None
        self.created_mode_name = None

    def get_connected_output(self):
        try:
            output = subprocess.check_output(["xrandr"]).decode("utf-8")
            # Look for "connected" line
            # eDP-1 connected 1536x1024+0+0 ...
            match = re.search(r"^(\S+) connected (?:primary )?(\d+x\d+)", output, re.MULTILINE)
            if match:
                return match.group(1), match.group(2)
            # Fallback if no resolution set yet (maybe?)
            match = re.search(r"^(\S+) connected", output, re.MULTILINE)
            if match:
                return match.group(1), None
        except Exception as e:
            logger.error(f"Error getting connected output: {e}")
        return None, None

    def create_and_set_mode(self, width, height, refresh_rate=60):
        output_name, current_mode = self.get_connected_output()
        if not output_name:
            logger.error("No connected output found")
            return False
        
        self.output_name = output_name
        if not self.original_mode:
            self.original_mode = current_mode
            logger.info(f"Saved original mode: {self.original_mode} for {self.output_name}")

        # Generate modeline
        try:
            cvt_out = subprocess.check_output(["cvt", str(width), str(height), str(refresh_rate)]).decode("utf-8")
            # Modeline "2560x1440_60.00"  312.25  2560 2752 3024 3488  1440 1443 1448 1493 -hsync +vsync
            modeline_match = re.search(r'Modeline\s+"([^"]+)"\s+(.*)', cvt_out)
            if not modeline_match:
                logger.error("Could not parse cvt output")
                return False
            
            mode_name = modeline_match.group(1)
            mode_params = modeline_match.group(2)
            self.created_mode_name = mode_name

            # Check if mode already exists
            xrandr_out = subprocess.check_output(["xrandr"]).decode("utf-8")
            if mode_name not in xrandr_out:
                # Create new mode
                logger.info(f"Creating new mode: {mode_name}")
                subprocess.check_call(["xrandr", "--newmode", mode_name] + mode_params.split())
            
            # Add mode to output
            try:
                subprocess.check_call(["xrandr", "--addmode", output_name, mode_name])
            except subprocess.CalledProcessError:
                pass # Might already be added

            # Set mode
            logger.info(f"Switching {output_name} to {mode_name}")
            subprocess.check_call(["xrandr", "--output", output_name, "--mode", mode_name])
            
            # Update screen dimensions for mouse normalization
            # We might need to update the streamer's knowledge of screen size
            return True

        except Exception as e:
            logger.error(f"Failed to set resolution: {e}")
            return False

    def restore(self):
        if self.output_name and self.original_mode:
            try:
                logger.info(f"Restoring resolution to {self.original_mode} on {self.output_name}")
                subprocess.check_call(["xrandr", "--output", self.output_name, "--mode", self.original_mode])
                
                if self.created_mode_name:
                    # Remove mode
                    # Note: removing mode while it's in use (if restore failed) is bad, but we just switched away.
                    try:
                        subprocess.check_call(["xrandr", "--delmode", self.output_name, self.created_mode_name])
                        subprocess.check_call(["xrandr", "--rmmode", self.created_mode_name])
                    except Exception as e:
                        logger.warning(f"Could not remove mode {self.created_mode_name}: {e}")
                    
                    self.created_mode_name = None
            except Exception as e:
                logger.error(f"Failed to restore resolution: {e}")
