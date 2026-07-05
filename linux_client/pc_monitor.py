import time
import sys
import glob
import psutil
import serial
import os

# ==============================================================================
# ESP32 UI CONFIGURATION
# ==============================================================================
# Change these values to configure the UI on the ESP32!
# Values will be automatically sent when the script starts.
# Colors must be 6-character HEX strings (without '#')
CFG_ACTIVE_SCREEN = 1      # 0 = Unified, 1 = Split Ring
CFG_BG_COLOR      = "FFFFFF" # White background
CFG_COLOR_CPU     = "0071C5" # Intel Blue
CFG_COLOR_GPU     = "76B900" # NVIDIA Green
CFG_COLOR_RAM     = "888888" # Gray
# ==============================================================================
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

        self.last_amdgpu_top_load = 0
        self.last_amdgpu_top_time = 0
        
        self.last_nvtop_load = 0
        self.last_nvtop_time = 0

    def _get_nvtop_load(self):
        current_time = time.time()
        if current_time - self.last_nvtop_time < 1.0:
            return self.last_nvtop_load
            
        import subprocess
        import json
        try:
            out = subprocess.check_output(['nvtop', '-s'], stderr=subprocess.DEVNULL, timeout=1)
            # fix potential trailing commas or malformed json from nvtop versions
            out_str = out.decode('utf-8').strip()
            # simple attempt to parse
            try:
                data = json.loads(out_str)
                if isinstance(data, dict) and "Devices" in data:
                    data = data["Devices"]
                if isinstance(data, list) and len(data) > 0:
                    device = data[0]
                    
                    if "GPU rate" in device:
                        self.last_nvtop_load = int(device["GPU rate"])
                    elif "gpu_utilization_percent" in device:
                        self.last_nvtop_load = int(device["gpu_utilization_percent"])
                    elif "gpu_utilization_rate" in device:
                        self.last_nvtop_load = int(device["gpu_utilization_rate"])
            except json.JSONDecodeError:
                pass
        except Exception:
            pass
            
        self.last_nvtop_time = current_time
        return self.last_nvtop_load

    def _get_amdgpu_top_load(self):
        current_time = time.time()
        if current_time - self.last_amdgpu_top_time < 1.0:
            return self.last_amdgpu_top_load
            
        import subprocess
        import json
        try:
            # -n 2 esegue due cicli. Il primo spesso è 0. Il secondo ha la misurazione di 100ms (-s 100).
            out = subprocess.check_output(
                ['amdgpu_top', '--json', '-s', '100', '-n', '2'], 
                stderr=subprocess.DEVNULL,
                timeout=1
            )
            lines = out.decode('utf-8').strip().split('\n')
            if len(lines) > 0:
                data = json.loads(lines[-1])
                if isinstance(data, list) and len(data) > 0:
                    data = data[0]
                if isinstance(data, dict) and "devices" in data:
                    devices = data["devices"]
                    if isinstance(devices, list) and len(devices) > 0:
                        device = devices[0]
                        load = 0
                        activity = device.get("gpu_activity", {})
                        gfx = activity.get("GFX", {})
                        if isinstance(gfx, dict) and "value" in gfx:
                            load = int(gfx["value"])
                        else:
                            fdinfo = device.get("fdinfo", {})
                            if isinstance(fdinfo, dict):
                                total_load = 0
                                for pid, info in fdinfo.items():
                                    if isinstance(info, dict):
                                        proc_gfx = info.get("gfx", {})
                                        proc_dec = info.get("dec", {})
                                        if isinstance(proc_gfx, dict):
                                            total_load += int(proc_gfx.get("value", 0))
                                        if isinstance(proc_dec, dict):
                                            total_load += int(proc_dec.get("value", 0))
                                load = min(100, total_load)
                        self.last_amdgpu_top_load = load
        except Exception:
            pass
            
        self.last_amdgpu_top_time = current_time
        return self.last_amdgpu_top_load

    def _find_amd_gpu(self):
        # 1. Try standard AMD Vendor ID
        for card in glob.glob('/sys/class/drm/card[0-9]*'):
            vendor_path = os.path.join(card, 'device/vendor')
            if os.path.exists(vendor_path):
                try:
                    with open(vendor_path, 'r') as f:
                        if f.read().strip() == '0x1002': # AMD PCI Vendor ID
                            return card
                except: pass
                
        # 2. Check if any card has an hwmon named 'amdgpu'
        for card in glob.glob('/sys/class/drm/card[0-9]*'):
            hwmons = glob.glob(os.path.join(card, 'device/hwmon/hwmon*'))
            for hwmon in hwmons:
                name_path = os.path.join(hwmon, 'name')
                if os.path.exists(name_path):
                    try:
                        with open(name_path, 'r') as f:
                            if f.read().strip() == 'amdgpu':
                                return card
                    except: pass
                    
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
        load, temp, vram_used, vram_total = 0, -1, 0, 0
        if not self.amd_gpu_path:
            return load, temp, vram_used, vram_total

        try:
            busy_path = f"{self.amd_gpu_path}/device/gpu_busy_percent"
            if os.path.exists(busy_path):
                with open(busy_path, 'r') as f:
                    load = int(f.read().strip())
            else:
                load = self._get_nvtop_load()
                if load == 0:
                    load = self._get_amdgpu_top_load()
        except Exception:
            load = self._get_nvtop_load()
            if load == 0:
                load = self._get_amdgpu_top_load()

        try:
            # Find hwmon for temperature
            hwmon_path = glob.glob(os.path.join(self.amd_gpu_path, 'device/hwmon/hwmon*'))
            if hwmon_path:
                for temp_file in ['temp1_input', 'temp2_input', 'temp3_input']:
                    temp_path = os.path.join(hwmon_path[0], temp_file)
                    if os.path.exists(temp_path):
                        with open(temp_path, 'r') as f:
                            temp = int(f.read().strip()) / 1000.0
                        break
        except Exception:
            pass

        try:
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

    def get_ram_temp(self):
        if not hasattr(psutil, "sensors_temperatures"):
            return -1
        temps = psutil.sensors_temperatures()
        if not temps:
            return -1
        
        # 1. Try known RAM sensor modules (jc42 for DDR4, spd5118 for DDR5)
        for name in ['jc42', 'spd5118']:
            if name in temps and len(temps[name]) > 0:
                return temps[name][0].current
                
        # 2. Try looking for 'ram', 'dimm', or 'memory' in the label of any sensor (useful for Nuvoton chips like nct6683)
        for name, entries in temps.items():
            for entry in entries:
                label = entry.label.lower() if entry.label else ""
                if "ram" in label or "dimm" in label or "memory" in label:
                    return entry.current
                    
        return -1

    def get_stats(self):
        # CPU
        cpu_load = psutil.cpu_percent(interval=None)
        cpu_temp = self.get_cpu_temp()
        
        # RAM
        ram = psutil.virtual_memory()
        ram_used = ram.used / (1024**3)
        ram_total = ram.total / (1024**3)
        ram_temp = self.get_ram_temp()
        
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
            "RAMT": round(ram_temp, 1),
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
        print("====== HARDWARE MONITOR TEST MODE ======")
        print(f"Detected GPU Vendor: {monitor.gpu_vendor}")
        
        if hasattr(psutil, "sensors_temperatures"):
            print("--- Sensori di Temperatura Disponibili ---")
            temps = psutil.sensors_temperatures()
            for name, entries in temps.items():
                for entry in entries:
                    label = entry.label if entry.label else "N/A"
                    print(f" - {name} ({label}): {entry.current}C")
            print("------------------------------------------")

        while True:
            stats = monitor.get_stats()
            data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},RAMT:{stats['RAMT']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
            print(data_str.strip())
            time.sleep(1)

    print("Scanning for Scarab Monitor...")
    port = find_scarab_port()
    if not port:
        print("Error: Could not find Scarab Monitor on any /dev/ttyUSB* or /dev/ttyACM* port.")
        sys.exit(1)
        
    print(f"Connected to {port}")
    
    with serial.Serial(port, 115200) as ser:
        # Send UI configuration
        cfg_str = f"CFG:SCR={CFG_ACTIVE_SCREEN},BG={CFG_BG_COLOR},CCPU={CFG_COLOR_CPU},CGPU={CFG_COLOR_GPU},CRAM={CFG_COLOR_RAM}\n"
        print(f"Sending UI Config: {cfg_str.strip()}")
        ser.write(cfg_str.encode('ascii'))
        time.sleep(0.5)
        
        while True:
            try:
                stats = monitor.get_stats()
                data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},RAMT:{stats['RAMT']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
                ser.write(data_str.encode('ascii'))
                time.sleep(0.1) # 10 FPS
            except Exception as e:
                print(f"Error: {e}")
                time.sleep(1)

if __name__ == "__main__":
    main()
