import customtkinter as ctk
from tkinter import ttk, Canvas
import serial
import math
import random
import time
import threading
import json
import re
import os
from datetime import datetime, timedelta

# ==========================================
#               CONFIGURATION
# ==========================================

import serial.tools.list_ports

# --- HARDWARE CONFIG (SHARED) ---
BAUD_RATE = 9600

# --- VISUALIZER LOOK & FEEL ---
VIS_BG_COLOR = "#1e1e1e"
CIRCLE_COLOR = "#00ffff"
BASE_RADIUS = 120
MAX_GROWTH = 120
MAX_BUZZ_NOISE = 15
LERP_SPEED = 0.1

# --- DASHBOARD SETTINGS ---
LOG_DIR = "logs"

# --- GLOBAL FLAGS ---
USE_MOCK_DATA = False

# ==========================================
#           HELPER LOGIC
# ==========================================

def lerp(start, end, alpha):
    return start + (end - start) * alpha

# ==========================================
#           MAIN APPLICATION CLASS
# ==========================================

ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("dark-blue")

class UnifiedHapticApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        # 1. Window Setup
        self.title("Unified Haptic Dashboard (Shared Serial)")
        self.geometry("1600x900")
        self.bind("<F11>", self.toggle_fullscreen)
        self.fullscreen = False

        # Grid Layout
        self.grid_columnconfigure(0, weight=4)  # Visualizer (40%)
        self.grid_columnconfigure(1, weight=6)  # Data (60%)
        self.grid_rowconfigure(0, weight=1)

        # 2. State Variables
        self.running = True
        self.serial_devices = []
        self.buffers = {}

        # Visualizer State
        self.vis_state = "IDLE"
        self.last_timer_reset = 0
        self.initiation_start_time = 0
        self.smoothed_radius = BASE_RADIUS
        self.smoothed_noise = 0
        self.vis_center = (400, 300)

        # Ring Identity & Status
        # Maps port_name -> "Ring 1" (peripheral) or "Ring 2" (control)
        self.ring_map = {}
        # Tracks which serial ports are open
        self.port_connected = {}

        # Dashboard State
        self.arduino_epoch = None
        self.expecting_json = False

        # 3. Build UI
        self.setup_visualizer_frame()
        self.setup_dashboard_frame()

        # 4. Initialize Shared Hardware
        self.init_shared_hardware()

        # 5. Start Threads
        self.serial_thread = threading.Thread(target=self.serial_listener_loop, daemon=True)
        self.serial_thread.start()

        # Visualizer Animation Loop
        self.update_visualizer_loop()

        # 6. Global Inputs
        self.bind("<Key>", self.handle_keypress)

    def toggle_fullscreen(self, event=None):
        self.fullscreen = not self.fullscreen
        self.attributes("-fullscreen", self.fullscreen)

    # ==========================================
    #       SHARED HARDWARE MANAGER
    # ==========================================

    def init_shared_hardware(self):
        global USE_MOCK_DATA
        available = serial.tools.list_ports.comports()
        port_names = [p.device for p in available if p.device.upper() != "COM1"]
        print(f"--- DETECTED SERIAL PORTS: {port_names} ---")
        found = False

        for port in port_names:
            try:
                dev = serial.Serial(port, BAUD_RATE, timeout=0)
                self.serial_devices.append((port, dev))
                self.buffers[port] = ""
                self.port_connected[port] = True
                print(f"SUCCESS: Connected to {port}")
                found = True
            except Exception as e:
                self.port_connected[port] = False
                print(f"FAILED {port}: {e}")

        if found:
            self.status_btn.configure(text="CONNECTED", fg_color="#2b9348")
            USE_MOCK_DATA = False
        else:
            print("No devices found. Swapping to MOCK mode.")
            self.status_btn.configure(text="MOCK MODE", fg_color="#cf3a3a")
            USE_MOCK_DATA = True

    def serial_listener_loop(self):
        while self.running:
            if USE_MOCK_DATA:
                time.sleep(1)
                continue

            for port_name, device in self.serial_devices:
                try:
                    if device.in_waiting > 0:
                        chunk = device.read(device.in_waiting).decode('utf-8', errors='ignore')
                        self.buffers[port_name] += chunk

                        while '\n' in self.buffers[port_name]:
                            line, remainder = self.buffers[port_name].split('\n', 1)
                            self.buffers[port_name] = remainder
                            if line.strip():
                                # Pass port_name so we know which ring sent the message
                                self.after(0, self.dispatch_message, port_name, line.strip())
                except Exception as e:
                    print(f"Read Error {port_name}: {e}")

            time.sleep(0.01)

    def dispatch_message(self, port_name, line):
        # Auto-detect ring identity from unique messages
        if "Connected to central" in line or "Disconnected from central" in line:
            self.ring_map[port_name] = "Ring 1"
        elif "entering session loop" in line or "Disconnected from peripheral" in line:
            self.ring_map[port_name] = "Ring 2"
        # 1. Send to Visualizer
        self.process_vis_message(line)
        # 2. Send to Dashboard
        self.process_dashboard_message(line)

    # ==========================================
    #       PART 1: VISUALIZER LOGIC
    # ==========================================

    def setup_visualizer_frame(self):
        self.vis_frame = ctk.CTkFrame(self, fg_color="black")
        self.vis_frame.grid(row=0, column=0, sticky="nsew", padx=2, pady=2)

        self.vis_canvas = Canvas(self.vis_frame, bg=VIS_BG_COLOR, highlightthickness=0)
        self.vis_canvas.pack(fill="both", expand=True)
        self.vis_canvas.bind("<Configure>", self.on_vis_resize)

    def on_vis_resize(self, event):
        self.vis_center = (event.width // 2, event.height // 2)

    def process_vis_message(self, line):
        if "Entering initiatee" in line or "Entering initiator" in line:
            self.vis_state = "INITIATION"
            self.initiation_start_time = time.time()
        elif "waitForStart" in line:
            self.vis_state = "IDLE"
        elif "Entering mainLoop" in line:
            self.vis_state = "DECAYING CONSENT"
            self.last_timer_reset = time.time()
        elif "Timer reset" in line:
            if self.vis_state == "DECAYING CONSENT":
                self.last_timer_reset = time.time()
        elif "Exiting mainLoop" in line:
            self.vis_state = "IDLE"

    def update_visualizer_loop(self):
        if not self.running:
            return

        current_time = time.time()
        target_radius = BASE_RADIUS
        target_noise = 0
        status_text = f"State: {self.vis_state}"

        if self.vis_state == "INITIATION":
            elapsed_init = current_time - self.initiation_start_time
            cycle_pos = elapsed_init % 1.0
            if cycle_pos < 0.2:
                target_radius = BASE_RADIUS + (MAX_GROWTH * 0.8)
                target_noise = MAX_BUZZ_NOISE * 0.8
            else:
                target_radius = BASE_RADIUS
                target_noise = 0

        elif self.vis_state == "DECAYING CONSENT":
            elapsed = current_time - self.last_timer_reset
            if elapsed < 30:
                decay_factor = 1.0 - (elapsed / 30.0)
                target_radius = BASE_RADIUS + (MAX_GROWTH * decay_factor)
                target_noise = MAX_BUZZ_NOISE * decay_factor
                status_text += f"\nDecay: {30 - elapsed:.1f}s"
            else:
                target_radius = BASE_RADIUS
                target_noise = 0
                status_text += "\nDecay Complete"

        # Smoothing
        self.smoothed_radius = lerp(self.smoothed_radius, target_radius, LERP_SPEED)
        self.smoothed_noise = lerp(self.smoothed_noise, target_noise, LERP_SPEED)

        # Draw
        self.vis_canvas.delete("all")
        cx, cy = self.vis_center

        if self.smoothed_noise < 0.5:
            r = int(self.smoothed_radius)
            self.vis_canvas.create_oval(cx - r, cy - r, cx + r, cy + r, fill=CIRCLE_COLOR, outline="")
        else:
            points = []
            num_points = 120
            for i in range(num_points):
                angle = (2 * math.pi / num_points) * i
                noise_offset = random.uniform(-self.smoothed_noise, self.smoothed_noise)
                r_noisy = self.smoothed_radius + noise_offset
                x = cx + r_noisy * math.cos(angle)
                y = cy + r_noisy * math.sin(angle)
                points.extend([x, y])
            self.vis_canvas.create_polygon(points, fill=CIRCLE_COLOR, outline="")

        # State text (bottom-left)
        self.vis_canvas.create_text(20, cy + cy - 40, text=status_text, fill="#c8c8c8",
                                    font=("Consolas", 14), anchor="sw")

        # Ring status indicators (top)
        self.draw_ring_status()

        self.after(16, self.update_visualizer_loop)

    def draw_ring_status(self):
        """Draw serial port connection status at the top of the visualizer."""
        y = 25

        for i, port in enumerate(self.port_connected.keys()):
            x = 30
            y_offset = y + (i * 30)
            is_connected = self.port_connected.get(port, False)

            # Status dot
            dot_r = 6
            dot_color = "#2bff2b" if is_connected else "#ff3b3b"
            self.vis_canvas.create_oval(x - dot_r, y_offset - dot_r, x + dot_r, y_offset + dot_r,
                                        fill=dot_color, outline="")

            # Label: show port name and ring identity if known
            ring_label = self.ring_map.get(port, "Unknown")
            status_str = "Connected" if is_connected else "Disconnected"
            self.vis_canvas.create_text(x + dot_r + 10, y_offset,
                                        text=f"{port} ({ring_label}): {status_str}",
                                        fill="#c8c8c8", font=("Consolas", 12), anchor="w")

    def handle_keypress(self, event):
        if USE_MOCK_DATA:
            if event.char == '1':
                self.dispatch_message("MOCK", "Entering initiatee")
            elif event.char == '2':
                self.dispatch_message("MOCK", "waitForStart")
            elif event.char == '3':
                self.dispatch_message("MOCK", "Entering mainLoop")
            elif event.char == '4':
                self.dispatch_message("MOCK", "Timer reset")
            elif event.char == '5':
                self.dispatch_message("MOCK", "Exiting mainLoop")

    # ==========================================
    #       PART 2: DASHBOARD LOGIC
    # ==========================================

    def setup_dashboard_frame(self):
        self.right_frame = ctk.CTkFrame(self, fg_color="transparent")
        self.right_frame.grid(row=0, column=1, sticky="nsew", padx=20, pady=20)
        self.right_frame.grid_rowconfigure(2, weight=1)
        self.right_frame.grid_columnconfigure(0, weight=1)

        self.font_title = ctk.CTkFont(family="Roboto Medium", size=36)
        self.font_val = ctk.CTkFont(family="Roboto", size=60, weight="bold")
        self.font_btn = ctk.CTkFont(family="Roboto", size=18, weight="bold")

        # Header
        header = ctk.CTkFrame(self.right_frame, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew", pady=(0, 20))
        ctk.CTkLabel(header, text="CONSENT METRICS", font=self.font_title).pack(side="left")
        self.status_btn = ctk.CTkButton(header, text="SEARCHING...", fg_color="#cf3a3a",
                                        width=150, height=40, hover=False)
        self.status_btn.pack(side="right")

        # Stats
        stats = ctk.CTkFrame(self.right_frame, fg_color="transparent")
        stats.grid(row=1, column=0, sticky="ew", pady=10)
        stats.grid_columnconfigure((0, 1, 2), weight=1)

        self.lbl_p_inits = self.make_stat_card(stats, "Ring 1 Inits", 0, "#4cc9f0")
        self.lbl_c_inits = self.make_stat_card(stats, "Ring 2 Inits", 1, "#f72585")
        self.lbl_duration = self.make_stat_card(stats, "Avg ms", 2, "#ffffff")

        # Table
        t_cont = ctk.CTkFrame(self.right_frame, fg_color="#1a1a1a", corner_radius=10)
        t_cont.grid(row=2, column=0, sticky="nsew", pady=20)

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview", background="#2b2b2b", foreground="white", fieldbackground="#2b2b2b",
                        rowheight=40, font=("Roboto", 14), borderwidth=0)
        style.configure("Treeview.Heading", background="#3a3a3a", foreground="white", relief="flat",
                        font=("Roboto", 12, "bold"))

        cols = ("ID", "Initiator", "End Method", "Duration", "ReUps", "Time")
        self.tree = ttk.Treeview(t_cont, columns=cols, show="headings", selectmode="none")
        for c in cols:
            self.tree.heading(c, text=c)
            self.tree.column(c, anchor="center", width=80)

        scroll = ctk.CTkScrollbar(t_cont, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side="left", fill="both", expand=True, padx=5, pady=5)
        scroll.pack(side="right", fill="y", padx=5, pady=5)

        # Footer
        self.btn_delete = ctk.CTkButton(self.right_frame, text="WIPE DATA", command=self.send_delete_signal,
                                        fg_color="#9e2a2b", hover_color="#c1121f", height=50, font=self.font_btn)
        self.btn_delete.grid(row=3, column=0, sticky="ew", pady=10)

    def make_stat_card(self, parent, title, col, color):
        card = ctk.CTkFrame(parent, fg_color="#2b2b2b")
        card.grid(row=0, column=col, padx=10, sticky="ew")
        ctk.CTkLabel(card, text=title, font=("Roboto", 14), text_color="gray").pack(pady=(15, 0))
        lbl = ctk.CTkLabel(card, text="0", font=self.font_val, text_color=color)
        lbl.pack(pady=(0, 15))
        return lbl

    def process_dashboard_message(self, line):
        # 1. Sync Time
        match = re.search(r"Current millis\D*(\d+)", line)
        if match:
            ms = int(match.group(1))
            self.arduino_epoch = datetime.now() - timedelta(milliseconds=ms)

        # 2. Check for JSON Trigger
        if line == "Printing JSON Data":
            self.expecting_json = True
            return

        # 3. Capture JSON if triggered
        if self.expecting_json and line.startswith("{"):
            try:
                data = json.loads(line)
                self.update_ui_from_data(data)
                self.expecting_json = False
            except Exception as e:
                print(f"JSON Parse Error: {e}")
                self.expecting_json = False

    def update_ui_from_data(self, data):
        self.lbl_p_inits.configure(text=str(data.get('numPInits', 0)))
        self.lbl_c_inits.configure(text=str(data.get('numCInits', 0)))
        sessions = data.get('sessions', [])

        if sessions:
            avg = sum(s['duration'] for s in sessions) / len(sessions)
            self.lbl_duration.configure(text=f"{avg:.0f}")
        else:
            self.lbl_duration.configure(text="0")

        # Clear and Fill Table
        for item in self.tree.get_children():
            self.tree.delete(item)
        for s in sessions:
            init_txt = "Ring 1" if s['initiator'] == "Peripheral" else "Ring 2"
            end_txt = "Button" if s['endMethod'] == "Button" else "Decay"

            if self.arduino_epoch:
                real_time = self.arduino_epoch + timedelta(milliseconds=int(s['begin']))
                time_str = real_time.strftime("%H:%M:%S")
            else:
                time_str = f"+{int(s['begin']) / 1000:.1f}s"

            self.tree.insert("", "end", values=(
                s['id'], init_txt, end_txt, f"{s['duration']} ms", s['numReUps'], time_str
            ))

        # Log to File
        if not os.path.exists(LOG_DIR):
            os.makedirs(LOG_DIR)
        fname = f"{LOG_DIR}/log_{datetime.now().strftime('%H-%M-%S')}.json"
        try:
            with open(fname, 'w') as f:
                json.dump(data, f, indent=4)
        except:
            pass

    def send_delete_signal(self):
        for _, dev in self.serial_devices:
            if dev.is_open:
                try:
                    dev.write(b'd')
                except:
                    pass

if __name__ == "__main__":
    app = UnifiedHapticApp()
    app.mainloop()
