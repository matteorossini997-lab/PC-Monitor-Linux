import time
import sys
import glob
import psutil
import serial
import os

try:
    import pynvml
    NVML_AVAILABLE = True
except ImportError:
    NVML_AVAILABLE = False

class HardwareMonitor:
    def __init__(self):
        self.net_io_start = psutil.net_io_counters()
        self.last_time = time.time()
        self.amd_gpu_path = self._find_amd_gpu()
        
        if NVML_AVAILABLE:
            try:
                pynvml.nvmlInit()
                self.nvml_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
                self.gpu_vendor = "NVIDIA"
            except pynvml.NVMLError:
                self.gpu_vendor = "AMD" if self.amd_gpu_path else "UNKNOWN"
        else:
            self.gpu_vendor = "AMD" if self.amd_gpu_path else "UNKNOWN"

    def _find_amd_gpu(self):
        # Look for amdgpu sysfs paths
        for card in glob.glob('/sys/class/drm/card*'):
            vendor_path = os.path.join(card, 'device/vendor')
            if os.path.exists(vendor_path):
                with open(vendor_path, 'r') as f:
                    if f.read().strip() == '0x1002': # AMD PCI Vendor ID
                        return card
        return None

    def get_cpu_temp(self):
        if not hasattr(psutil, "sensors_temperatures"):
            return -1
        temps = psutil.sensors_temperatures()
        if not temps:
            return -1
        
        # Try common sensor names
        for name in ['coretemp', 'k10temp', 'zenpower', 'cpu_thermal']:
            if name in temps and len(temps[name]) > 0:
                return temps[name][0].current
                
        # Fallback to the first available if not found
        first_key = list(temps.keys())[0]
        return temps[first_key][0].current

    def get_amd_gpu_stats(self):
        load, temp, vram_used, vram_total = -1, -1, 0, 0
        if not self.amd_gpu_path:
            return load, temp, vram_used, vram_total

        try:
            busy_path = os.path.join(self.amd_gpu_path, 'device/gpu_busy_percent')
            if os.path.exists(busy_path):
                with open(busy_path, 'r') as f:
                    load = int(f.read().strip())
            
            # Find hwmon for temperature
            hwmon_path = glob.glob(os.path.join(self.amd_gpu_path, 'device/hwmon/hwmon*'))
            if hwmon_path:
                for temp_file in ['temp1_input', 'temp2_input', 'temp3_input']:
                    temp_path = os.path.join(hwmon_path[0], temp_file)
                    if os.path.exists(temp_path):
                        with open(temp_path, 'r') as f:
                            temp = int(f.read().strip()) / 1000.0
                        break

            # Try to get VRAM from standard amdgpu sysfs (available in recent kernels)
            vram_used_path = os.path.join(self.amd_gpu_path, 'device/mem_info_vram_used')
            vram_total_path = os.path.join(self.amd_gpu_path, 'device/mem_info_vram_total')
            if os.path.exists(vram_used_path) and os.path.exists(vram_total_path):
                with open(vram_used_path, 'r') as f:
                    vram_used = int(f.read().strip()) / (1024**3)
                with open(vram_total_path, 'r') as f:
                    vram_total = int(f.read().strip()) / (1024**3)
        except Exception:
            pass
            
        return load, temp, vram_used, vram_total

    def get_stats(self):
        # CPU
        cpu_load = psutil.cpu_percent(interval=None)
        cpu_temp = self.get_cpu_temp()
        
        # RAM
        ram = psutil.virtual_memory()
        ram_used = ram.used / (1024**3)
        ram_total = ram.total / (1024**3)
        
        # Network
        current_time = time.time()
        time_diff = current_time - self.last_time
        net_io = psutil.net_io_counters()
        
        if time_diff > 0:
            down_mbps = ((net_io.bytes_recv - self.net_io_start.bytes_recv) * 8) / (1024**2) / time_diff
            up_mbps = ((net_io.bytes_sent - self.net_io_start.bytes_sent) * 8) / (1024**2) / time_diff
        else:
            down_mbps, up_mbps = 0, 0
            
        self.net_io_start = net_io
        self.last_time = current_time
        
        # GPU
        gpu_load, gpu_temp, vram_used, vram_total = -1, -1, 0, 0
        if self.gpu_vendor == "NVIDIA":
            try:
                util = pynvml.nvmlDeviceGetUtilizationRates(self.nvml_handle)
                gpu_load = util.gpu
                gpu_temp = pynvml.nvmlDeviceGetTemperature(self.nvml_handle, pynvml.NVML_TEMPERATURE_GPU)
                mem = pynvml.nvmlDeviceGetMemoryInfo(self.nvml_handle)
                vram_used = mem.used / (1024**3)
                vram_total = mem.total / (1024**3)
            except pynvml.NVMLError:
                pass
        elif self.gpu_vendor == "AMD":
            gpu_load, gpu_temp, vram_used, vram_total = self.get_amd_gpu_stats()
            
        return {
            "CPU": int(cpu_load),
            "CPUT": round(cpu_temp, 1),
            "GPU": int(gpu_load),
            "GPUT": round(gpu_temp, 1),
            "VRAM": f"{vram_used:.1f}/{vram_total:.1f}",
            "RAM": f"{ram_used:.1f}/{ram_total:.1f}",
            "NET": "LAN",
            "SPEED": "1000 Mbps", # Mocked for now, not easily accessible cross-platform
            "DOWN": round(down_mbps, 1),
            "UP": round(up_mbps, 1)
        }

def find_scarab_port():
    ports = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
    for port in ports:
        try:
            with serial.Serial(port, 115200, timeout=2) as ser:
                ser.write(b"WHO_ARE_YOU?\n")
                response = ser.readline().decode('ascii', errors='ignore').strip()
                if "SCARAB_CLIENT_OK" in response:
                    return port
        except serial.SerialException:
            pass
    return None

def main():
    test_mode = "--test" in sys.argv
    monitor = HardwareMonitor()
    
    # Initialize cpu_percent
    psutil.cpu_percent(interval=None)
    time.sleep(0.5)

    if test_mode:
        print("Running in TEST MODE (No Serial Communication)")
        print(f"Detected GPU Vendor: {monitor.gpu_vendor}")
        while True:
            stats = monitor.get_stats()
            data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
            print(data_str.strip())
            time.sleep(1)

    print("Scanning for Scarab Monitor...")
    port = find_scarab_port()
    if not port:
        print("Error: Could not find Scarab Monitor on any /dev/ttyUSB* or /dev/ttyACM* port.")
        sys.exit(1)
        
    print(f"Connected to {port}")
    
    with serial.Serial(port, 115200) as ser:
        while True:
            try:
                stats = monitor.get_stats()
                data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
                ser.write(data_str.encode('ascii'))
                time.sleep(0.1) # 10 FPS
            except Exception as e:
                print(f"Error: {e}")
                time.sleep(1)

if __name__ == "__main__":
    main()
