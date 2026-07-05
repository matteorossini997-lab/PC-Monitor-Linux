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
        self.last_amdgpu_top_vram_temp = -1
        self.last_amdgpu_top_time = 0
        
        self.last_nvtop_load = 0
        self.last_nvtop_time = 0

    def _get_nvtop_stats(self):
        current_time = time.time()
        if hasattr(self, 'last_nvtop_time') and current_time - self.last_nvtop_time < 1.0:
            return getattr(self, 'last_nvtop_stats', (-1, -1, 0, 0, -1))
            
        import subprocess
        import json
        load, temp, vram_used, vram_total, power = -1, -1, 0, 0, -1
        try:
            out = subprocess.check_output(['nvtop', '-s'], stderr=subprocess.DEVNULL, timeout=1)
            out_str = out.decode('utf-8').strip()
            data = json.loads(out_str)
            if isinstance(data, dict) and "Devices" in data:
                data = data["Devices"]
            if isinstance(data, list) and len(data) > 0:
                device = data[0]
                
                # Load
                if "gpu_util" in device:
                    val = str(device["gpu_util"]).replace("%", "")
                    if val.isdigit(): load = int(val)
                elif "GPU rate" in device:
                    load = int(device["GPU rate"])
                    
                # Temp
                if "temp" in device:
                    val = str(device["temp"]).replace("C", "")
                    if val.isdigit(): temp = float(val)
                        
                # Power
                if "power_draw" in device:
                    val = str(device["power_draw"]).replace("W", "")
                    if val.isdigit(): power = float(val)
                        
                # VRAM
                if "mem_used" in device:
                    vram_used = float(device["mem_used"]) / (1024**3)
                if "mem_total" in device:
                    vram_total = float(device["mem_total"]) / (1024**3)
        except Exception:
            pass
            
        self.last_nvtop_stats = (load, temp, vram_used, vram_total, power)
        self.last_nvtop_time = current_time
        return self.last_nvtop_stats

    def _find_amd_gpu(self):
        for card in glob.glob('/sys/class/drm/card[0-9]*'):
            vendor_path = os.path.join(card, 'device/vendor')
            if os.path.exists(vendor_path):
                try:
                    with open(vendor_path, 'r') as f:
                        if f.read().strip() == '0x1002':
                            return card
                except: pass
                
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
        if not hasattr(psutil, "sensors_temperatures"): return -1
        temps = psutil.sensors_temperatures()
        if not temps: return -1
        
        for name in ['coretemp', 'k10temp', 'zenpower', 'cpu_thermal']:
            if name in temps and len(temps[name]) > 0:
                return temps[name][0].current
                
        first_key = list(temps.keys())[0]
        return temps[first_key][0].current

    def get_amd_gpu_stats(self):
        return self._get_nvtop_stats()

    def get_ram_temp(self):
        if not hasattr(psutil, "sensors_temperatures"): return -1
        temps = psutil.sensors_temperatures()
        if not temps: return -1
        for name in ['jc42', 'spd5118']:
            if name in temps and len(temps[name]) > 0: return temps[name][0].current
        for name, entries in temps.items():
            for entry in entries:
                label = entry.label.lower() if entry.label else ""
                if "ram" in label or "dimm" in label or "memory" in label:
                    return entry.current
        return -1

    def get_stats(self):
        cpu_load = psutil.cpu_percent(interval=None)
        cpu_temp = self.get_cpu_temp()
        
        ram = psutil.virtual_memory()
        ram_used = ram.used / (1024**3)
        ram_total = ram.total / (1024**3)
        
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
        
        gpu_load, gpu_temp, vram_used, vram_total, power = -1, -1, 0, 0, -1
        if self.gpu_vendor == "NVIDIA":
            try:
                util = pynvml.nvmlDeviceGetUtilizationRates(self.nvml_handle)
                gpu_load = util.gpu
                gpu_temp = pynvml.nvmlDeviceGetTemperature(self.nvml_handle, pynvml.NVML_TEMPERATURE_GPU)
                mem = pynvml.nvmlDeviceGetMemoryInfo(self.nvml_handle)
                vram_used = mem.used / (1024**3)
                vram_total = mem.total / (1024**3)
                power = pynvml.nvmlDeviceGetPowerUsage(self.nvml_handle) / 1000.0
            except pynvml.NVMLError:
                pass
        elif self.gpu_vendor == "AMD":
            gpu_load, gpu_temp, vram_used, vram_total, power = self.get_amd_gpu_stats()
            
        return {
            "CPU": int(cpu_load),
            "CPUT": round(cpu_temp, 1),
            "GPU": int(gpu_load),
            "GPUT": round(gpu_temp, 1),
            "VRAM": f"{vram_used:.1f}/{vram_total:.1f}",
            "PWR": round(power, 1),
            "RAM": f"{ram_used:.1f}/{ram_total:.1f}",
            "NET": "LAN",
            "SPEED": "1000 Mbps",
            "DOWN": round(down_mbps, 1),
            "UP": round(up_mbps, 1)
        }

def connect_scarab():
    import os
    if os.path.exists('/dev/ttyACM0'):
        try:
            ser = serial.Serial('/dev/ttyACM0', 115200, write_timeout=2)
            time.sleep(4) # Wait for ESP32 to fully boot and initialize USB CDC
            return ser
        except Exception:
            pass
    return None

def main():
    test_mode = "--test" in sys.argv
    monitor = HardwareMonitor()
    psutil.cpu_percent(interval=None)
    time.sleep(0.5)

    if test_mode:
        print("====== HARDWARE MONITOR TEST MODE ======")
        print(f"Detected GPU Vendor: {monitor.gpu_vendor}")
        while True:
            stats = monitor.get_stats()
            data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},PWR:{stats['PWR']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
            print(data_str.strip())
            time.sleep(1)

    print("Scanning for Scarab Monitor...")
    ser = connect_scarab()
    if not ser:
        print("Error: Could not find Scarab Monitor on any /dev/ttyUSB* or /dev/ttyACM* port.")
        sys.exit(1)
        
    print(f"Connected to {ser.port}")
    
    import threading
    def reader():
        while True:
            try:
                line = ser.readline()
                if line:
                    print(f"[ESP32] {line.decode('ascii', errors='ignore').strip()}")
            except Exception as e:
                pass
    threading.Thread(target=reader, daemon=True).start()

    cfg_str = f"CFG:SCR={CFG_ACTIVE_SCREEN},BG={CFG_BG_COLOR},CCPU={CFG_COLOR_CPU},CGPU={CFG_COLOR_GPU},CRAM={CFG_COLOR_RAM}\n"
    print(f"Sending UI Config: {cfg_str.strip()}", flush=True)
    ser.write(cfg_str.encode('ascii'))
    print("UI Config sent successfully", flush=True)
    time.sleep(0.5)
    
    print("Entering main loop", flush=True)
    while True:
        try:
            stats = monitor.get_stats()
            data_str = f"CPU:{stats['CPU']},CPUT:{stats['CPUT']},GPU:{stats['GPU']},GPUT:{stats['GPUT']},VRAM:{stats['VRAM']},RAM:{stats['RAM']},PWR:{stats['PWR']},NET:{stats['NET']},SPEED:{stats['SPEED']},DOWN:{stats['DOWN']},UP:{stats['UP']}\n"
            print(f"[PC] Sending: {data_str.strip()}", flush=True)
            ser.write(data_str.encode('ascii'))
            time.sleep(0.1)
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(1)

if __name__ == "__main__":
    main()
