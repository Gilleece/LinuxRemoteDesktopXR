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

        try:
            # Use --scale-from to create a virtual resolution
            # This forces the framebuffer to the target size while keeping the monitor at its native resolution
            logger.info(f"Setting virtual resolution {width}x{height} on {output_name} using scale-from")
            subprocess.check_call([
                "xrandr", 
                "--output", output_name, 
                "--scale-from", f"{width}x{height}"
            ])
            return True

        except Exception as e:
            logger.error(f"Failed to set resolution: {e}")
            return False

    def restore(self):
        if self.output_name:
            try:
                logger.info(f"Restoring display on {self.output_name}")
                # Reset scale to 1x1
                subprocess.check_call([
                    "xrandr", 
                    "--output", self.output_name, 
                    "--scale", "1x1"
                ])
                
                # Restore original mode if needed
                if self.original_mode:
                     subprocess.check_call(["xrandr", "--output", self.output_name, "--mode", self.original_mode])
                     
            except Exception as e:
                logger.error(f"Failed to restore resolution: {e}")
