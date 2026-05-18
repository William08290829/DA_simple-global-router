import argparse
import sys
import tkinter as tk
from tkinter import ttk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import matplotlib.patches as mpatches
from matplotlib.patches import Rectangle
import re
from collections import defaultdict

# 定義不同 layer 的顏色 (for routing)
LAYER_COLORS = {
    1: '#FF0000', # Red
    2: '#0000FF', # Blue
    3: '#008000', # Green
    4: '#FFA500', # Orange
    5: '#800080', # Purple
    6: '#00FFFF', # Cyan
    7: '#FF00FF', # Magenta
    8: '#A52A2A', # Brown
}

def get_layer_color(layer):
    return LAYER_COLORS.get(layer, '#000000') # 預設黑色

def parse_routing_file(filepath):
    grid_size = (0, 0)
    nets = {}
    current_net = None
    
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line.startswith('Gcell_grid'):
                    parts = line.split()
                    grid_size = (int(parts[1]), int(parts[2]))
                elif line.startswith('N'):
                    current_net = line
                    nets[current_net] = []
                elif line.startswith('M'):
                    match = re.match(r'M(\d+)\s+\((\d+),(\d+)\)\s+\((\d+),(\d+)\)', line)
                    if match:
                        layer = int(match.group(1))
                        x1, y1 = int(match.group(2)), int(match.group(3))
                        x2, y2 = int(match.group(4)), int(match.group(5))
                        nets[current_net].append({'layer': layer, 'p1': (x1, y1), 'p2': (x2, y2)})
    except Exception as e:
        print(f"Error reading routing file: {e}")
        sys.exit(1)
        
    return grid_size, nets

def parse_congestion_file(filepath):
    layers_info = {} # layer_name -> layer_idx
    edges = defaultdict(list)
    grid_size = (0, 0)
    
    if not filepath:
        return grid_size, layers_info, edges
        
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if line.startswith('GRID'):
                    parts = line.split()
                    grid_size = (int(parts[1]), int(parts[2]))
                elif line.startswith('LAYER'):
                    parts = line.split()
                    # LAYER <layer_name> <preferred_direction>
                    if len(parts) >= 3:
                        layer_name = parts[1]
                        # extract numeric layer for sorting (e.g. M1 -> 1)
                        match = re.search(r'\d+', layer_name)
                        layer_idx = int(match.group()) if match else 0
                        layers_info[layer_name] = layer_idx
                elif line.startswith('EDGE'):
                    parts = line.split()
                    # EDGE <layer_name> <orientation> <x1> <y1> <x2> <y2> <capacity> <usage>
                    if len(parts) >= 9:
                        layer_name = parts[1]
                        orient = parts[2]
                        x1, y1 = int(parts[3]), int(parts[4])
                        x2, y2 = int(parts[5]), int(parts[6])
                        cap, usage = int(parts[7]), int(parts[8])
                        edges[layer_name].append({
                            'p1': (x1, y1), 'p2': (x2, y2), 
                            'orient': orient, 'cap': cap, 'usage': usage
                        })
    except Exception as e:
        print(f"Error reading congestion map file: {e}")
        sys.exit(1)
        
    return grid_size, layers_info, edges

def merge_segments(segments):
    lines = defaultdict(lambda: {'H': defaultdict(list), 'V': defaultdict(list)})
    for seg in segments:
        layer = seg['layer']
        x1, y1 = seg['p1']
        x2, y2 = seg['p2']
        if x1 > x2: x1, x2 = x2, x1
        if y1 > y2: y1, y2 = y2, y1
        if y1 == y2:
            lines[layer]['H'][y1].append((x1, x2))
        elif x1 == x2:
            lines[layer]['V'][x1].append((y1, y2))
            
    merged = []
    for layer, orientations in lines.items():
        for y, intervals in orientations['H'].items():
            intervals.sort(key=lambda x: x[0])
            m = []
            for it in intervals:
                if not m:
                    m.append(list(it))
                else:
                    if it[0] <= m[-1][1]:
                        m[-1][1] = max(m[-1][1], it[1])
                    else:
                        m.append(list(it))
            for x1, x2 in m:
                merged.append({'layer': layer, 'p1': (x1, y), 'p2': (x2, y)})
                
        for x, intervals in orientations['V'].items():
            intervals.sort(key=lambda x: x[0])
            m = []
            for it in intervals:
                if not m:
                    m.append(list(it))
                else:
                    if it[0] <= m[-1][1]:
                        m[-1][1] = max(m[-1][1], it[1])
                    else:
                        m.append(list(it))
            for y1, y2 in m:
                merged.append({'layer': layer, 'p1': (x, y1), 'p2': (x, y2)})
    return merged

def create_routing_icon():
    img = tk.PhotoImage(width=20, height=20)
    img.put('#0078D7', to=(3, 14, 11, 16))
    img.put('#0078D7', to=(10, 5, 12, 16))
    img.put('#0078D7', to=(10, 5, 18, 7))
    img.put('#D83B01', to=(2, 13, 5, 16))
    img.put('#D83B01', to=(16, 4, 19, 7))
    return img

def create_congestion_icon():
    img = tk.PhotoImage(width=20, height=20)
    colors = [
        ['#151E3F', '#FEC601', '#151E3F'],
        ['#FEC601', '#931621', '#EA7317'],
        ['#151E3F', '#EA7317', '#151E3F']
    ]
    for r in range(3):
        for c in range(3):
            x1 = 2 + c*6
            y1 = 2 + r*6
            img.put(colors[r][c], to=(x1, y1, x1+5, y1+5))
    return img

def parse_original_input_file(filepath):
    pins = defaultdict(list)
    if not filepath:
        return pins
        
    try:
        with open(filepath, 'r') as f:
            in_nets = False
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if line == 'nets':
                    in_nets = True
                elif line == 'end_of_nets':
                    in_nets = False
                elif in_nets:
                    parts = line.split()
                    if not parts: continue
                    net_name = parts[0]
                    # Parse tuples like (x y)
                    matches = re.findall(r'\((\d+)\s+(\d+)\)', line)
                    for m in matches:
                        pins[net_name].append((int(m[0]), int(m[1])))
    except Exception as e:
        print(f"Error reading original input file: {e}")
        
    return pins

class VisualizerApp:
    def __init__(self, root, grid_size, nets, cong_grid, cong_layers, cong_edges, has_congestion, pins):
        self.root = root
        self.root.title("Global Routing & Congestion Visualizer")
        
        # Routing Data
        self.grid_size = grid_size
        self.nets = nets
        self.net_names = list(self.nets.keys())
        self.pins = pins
        
        # Congestion Data
        self.cong_grid = cong_grid if has_congestion else grid_size
        self.cong_layers = cong_layers # dict layer_name -> layer_idx
        self.cong_edges = cong_edges
        self.has_congestion = has_congestion
        
        # 建立主框架
        self.main_frame = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        self.main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 左側控制面板
        self.control_frame = ttk.Frame(self.main_frame, width=220)
        self.main_frame.add(self.control_frame, weight=0)
        
        # View Mode Toggle (只有在有提供 congestion_map 時才有用)
        self.view_mode = tk.StringVar(value="routing")
        
        mode_frame = ttk.LabelFrame(self.control_frame, text="View Mode")
        mode_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # 使用容器來讓按鈕水平並排
        btn_container = ttk.Frame(mode_frame)
        btn_container.pack(fill=tk.X, padx=2, pady=2)
        
        # 使用 style="Toolbutton" 讓它看起來像真的按鈕
        self.icon_rout = create_routing_icon()
        self.icon_cong = create_congestion_icon()
        
        rb_rout = ttk.Radiobutton(btn_container, text=" Routing", image=self.icon_rout, compound=tk.LEFT, variable=self.view_mode, value="routing", command=self.update_ui_for_mode, style="Toolbutton")
        rb_rout.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        
        rb_cong = ttk.Radiobutton(btn_container, text=" Congestion", image=self.icon_cong, compound=tk.LEFT, variable=self.view_mode, value="congestion", command=self.update_ui_for_mode, style="Toolbutton")
        rb_cong.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        
        if not self.has_congestion:
            rb_cong.config(state=tk.DISABLED)
            
        # 選單的標題
        self.list_label = ttk.Label(self.control_frame, text="Nets Selection:", font=("Arial", 10, "bold"))
        self.list_label.pack(pady=5, padx=5, anchor=tk.W)
        
        # Routing 的按鈕
        self.btn_frame = ttk.Frame(self.control_frame)
        self.btn_frame.pack(fill=tk.X, padx=5, pady=0)
        ttk.Button(self.btn_frame, text="Select All", command=self.select_all_nets).pack(side=tk.LEFT, expand=True, fill=tk.X)
        ttk.Button(self.btn_frame, text="Clear All", command=self.clear_all_nets).pack(side=tk.LEFT, expand=True, fill=tk.X)
        
        # 捲動選單
        self.listbox_frame = ttk.Frame(self.control_frame)
        self.listbox_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.canvas_list = tk.Canvas(self.listbox_frame, highlightthickness=0)
        self.scrollbar = ttk.Scrollbar(self.listbox_frame, orient="vertical", command=self.canvas_list.yview)
        self.scrollable_frame = ttk.Frame(self.canvas_list)
        
        self.scrollable_frame.bind("<Configure>", lambda e: self.canvas_list.configure(scrollregion=self.canvas_list.bbox("all")))
        self.canvas_list.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas_list.configure(yscrollcommand=self.scrollbar.set)
        
        self.canvas_list.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")
        
        def _on_mousewheel(event):
            if event.num == 5 or event.delta < 0:
                self.canvas_list.yview_scroll(1, "units")
            elif event.num == 4 or event.delta > 0:
                self.canvas_list.yview_scroll(-1, "units")

        self.canvas_list.bind_all("<MouseWheel>", _on_mousewheel)
        self.canvas_list.bind_all("<Button-4>", _on_mousewheel)
        self.canvas_list.bind_all("<Button-5>", _on_mousewheel)
        
        # 預先準備好 Routing Checkbuttons 與 Congestion Radiobuttons
        self.routing_widgets = []
        self.check_vars = {}
        for net in self.net_names:
            var = tk.BooleanVar(value=True)
            cb = ttk.Checkbutton(self.scrollable_frame, text=net, variable=var, command=self.draw_plot)
            self.routing_widgets.append(cb)
            self.check_vars[net] = var
            
        self.cong_widgets = []
        self.selected_cong_layer = tk.StringVar()
        
        if self.has_congestion:
            # Sort layers M1, M2, M3...
            sorted_layers = sorted(self.cong_layers.keys(), key=lambda k: self.cong_layers[k])
            if sorted_layers:
                self.selected_cong_layer.set(sorted_layers[0]) # 預設選第一個
            for layer in sorted_layers:
                rb = ttk.Radiobutton(self.scrollable_frame, text=layer, variable=self.selected_cong_layer, value=layer, command=self.draw_plot)
                self.cong_widgets.append(rb)
        
        # 右側繪圖區
        self.plot_frame = ttk.Frame(self.main_frame)
        self.main_frame.add(self.plot_frame, weight=1)
        
        self.fig, self.ax = plt.subplots(figsize=(8, 6))
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.plot_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        self.toolbar = NavigationToolbar2Tk(self.canvas, self.plot_frame)
        self.toolbar.update()
        
        self.update_ui_for_mode()
        
    def update_ui_for_mode(self):
        mode = self.view_mode.get()
        
        # 先清空目前的 widgets
        for widget in self.scrollable_frame.winfo_children():
            widget.pack_forget()
            
        if mode == "routing":
            self.list_label.config(text="Nets Selection:")
            self.btn_frame.pack(fill=tk.X, padx=5, pady=0, before=self.listbox_frame)
            for cb in self.routing_widgets:
                cb.pack(anchor="w", padx=2, pady=2)
        else:
            self.list_label.config(text="Layer Selection:")
            self.btn_frame.pack_forget() # 隱藏全選按鈕
            for rb in self.cong_widgets:
                rb.pack(anchor="w", padx=2, pady=2)
                
        self.draw_plot()
        
    def select_all_nets(self):
        for var in self.check_vars.values():
            var.set(True)
        self.draw_plot()
        
    def clear_all_nets(self):
        for var in self.check_vars.values():
            var.set(False)
        self.draw_plot()

    def get_selected_nets(self):
        return [net for net, var in self.check_vars.items() if var.get()]

    def draw_plot(self):
        self.ax.clear()
        mode = self.view_mode.get()
        
        if mode == "routing":
            self.draw_routing_plot()
        else:
            self.draw_congestion_plot()
            
        self.canvas.draw()
        
    def draw_routing_plot(self):
        max_x, max_y = self.grid_size
        self.ax.set_xlim(-0.5, max_x - 0.5)
        self.ax.set_ylim(-0.5, max_y - 0.5)
        self.ax.set_xticks(range(max_x))
        self.ax.set_yticks(range(max_y))
        self.ax.grid(True, linestyle='--', color='gray', alpha=0.5)
        self.ax.set_aspect('equal')
        
        layers_used = set()
        selected_nets = self.get_selected_nets()
        w = 0.2  
        
        for net_name in selected_nets:
            segments = self.nets[net_name]
            pt_layers = defaultdict(set)
            pt_degree = defaultdict(int)
            
            for seg in segments:
                pt_layers[seg['p1']].add(seg['layer'])
                pt_layers[seg['p2']].add(seg['layer'])
                pt_degree[seg['p1']] += 1
                pt_degree[seg['p2']] += 1
                
            merged_segments = merge_segments(segments)
            for seg in merged_segments:
                layer = seg['layer']
                layers_used.add(layer)
                color = get_layer_color(layer)
                
                x1, y1 = seg['p1']
                x2, y2 = seg['p2']
                hatch_pattern = '///' if layer % 2 == 1 else '\\\\\\\\'
                
                if x1 == x2:
                    min_y, max_y = min(y1, y2), max(y1, y2)
                    rect = Rectangle((x1 - w/2, min_y - w/2), w, (max_y - min_y) + w,
                                     facecolor=color, edgecolor='black', hatch=hatch_pattern, alpha=0.75, zorder=2)
                    self.ax.add_patch(rect)
                elif y1 == y2:
                    min_x, max_x = min(x1, x2), max(x1, x2)
                    rect = Rectangle((min_x - w/2, y1 - w/2), (max_x - min_x) + w, w,
                                     facecolor=color, edgecolor='black', hatch=hatch_pattern, alpha=0.75, zorder=2)
                    self.ax.add_patch(rect)
                else:
                    self.ax.plot([x1, x2], [y1, y2], color=color, linewidth=10, zorder=2, alpha=0.75)
                
            net_pins = self.pins.get(net_name, [])
            
            # 畫 Via (如果有大於一層通過該點，且該點不是 Pin)
            for pt, layers in pt_layers.items():
                x, y = pt
                if len(layers) > 1 and pt not in net_pins:
                    self.ax.plot(x, y, marker='s', color='black', markersize=6, zorder=4)
                    
            # 畫 Pin (嚴格依照 input_file)
            for pt in net_pins:
                self.ax.plot(pt[0], pt[1], marker='o', markerfacecolor='#DDDDDD', markeredgecolor='black', markersize=10, zorder=5)

        legend_elements = []
        for layer in sorted(layers_used):
            color = get_layer_color(layer)
            hatch_pattern = '///' if layer % 2 == 1 else '\\\\\\\\'
            legend_elements.append(mpatches.Patch(facecolor=color, edgecolor='black', hatch=hatch_pattern, label=f'Layer M{layer}', alpha=0.75))
            
        legend_elements.append(Line2D([0], [0], marker='o', color='w', markerfacecolor='#DDDDDD', markeredgecolor='black', markersize=10, label='Pin'))
        legend_elements.append(Line2D([0], [0], marker='s', color='w', markerfacecolor='black', markersize=6, label='Via'))
        
        if legend_elements:
            self.ax.legend(handles=legend_elements, loc='upper right', bbox_to_anchor=(1.15, 1))
            
        self.ax.set_title("Global Routing Output")
        self.ax.set_xlabel("X")
        self.ax.set_ylabel("Y")
        self.fig.tight_layout()

    def get_congestion_color(self, cap, usage):
        if cap == 0:
            return 'none' # Transparent
        ratio = usage / cap
        if ratio == 0:
            return '#151E3F' # DarkBlue
        elif ratio < 1.0:
            return '#FEC601' #Yellow
        elif ratio == 1.0:
            return '#EA7317' # Orange
        else:
            return '#931621' # Dark Red

    def draw_congestion_plot(self):
        max_x, max_y = self.cong_grid
        self.ax.set_xlim(-0.5, max_x - 0.5)
        self.ax.set_ylim(-0.5, max_y - 0.5)
        self.ax.set_xticks(range(max_x))
        self.ax.set_yticks(range(max_y))
        
        # 繪製較淺的背景格線
        self.ax.grid(True, linestyle=':', color='lightgray', alpha=0.5)
        self.ax.set_aspect('equal')
        
        layer = self.selected_cong_layer.get()
        if not layer:
            return
            
        edges = self.cong_edges.get(layer, [])
        w = 0.15 # 稍微調細 Edge 粗度
        
        for edge in edges:
            x1, y1 = edge['p1']
            x2, y2 = edge['p2']
            cap = edge['cap']
            usage = edge['usage']
            
            color = self.get_congestion_color(cap, usage)
            
            # 使用矩形來繪製，比較粗且清楚
            if x1 == x2: # Vertical
                min_y, max_y = min(y1, y2), max(y1, y2)
                rect = Rectangle((x1 - w/2, min_y), w, max_y - min_y, facecolor=color, edgecolor='none', zorder=2)
                self.ax.add_patch(rect)
                # 如果沒有 capacity，畫個虛線框
                if cap == 0:
                    rect.set_edgecolor('gray')
                    rect.set_linestyle('--')
                    rect.set_linewidth(1)
            elif y1 == y2: # Horizontal
                min_x, max_x = min(x1, x2), max(x1, x2)
                rect = Rectangle((min_x, y1 - w/2), max_x - min_x, w, facecolor=color, edgecolor='none', zorder=2)
                self.ax.add_patch(rect)
                if cap == 0:
                    rect.set_edgecolor('gray')
                    rect.set_linestyle('--')
                    rect.set_linewidth(1)

        # 繪製 Grid 點 (GCell centers)
        for x in range(max_x):
            for y in range(max_y):
                self.ax.plot(x, y, marker='o', color='black', markersize=3, zorder=3)

        # 建立 Legend
        legend_elements = [
            mpatches.Patch(color='#151E3F', label='Empty (Usage=0)'),
            mpatches.Patch(color='#FEC601', label='Safe (Usage < Cap)'),
            mpatches.Patch(color='#EA7317', label='Full (Usage == Cap)'),
            mpatches.Patch(color='#931621', label='Overflow (Usage > Cap)'),
            mpatches.Patch(facecolor='none', edgecolor='gray', linestyle='--', label='No Capacity (Cap=0)')
        ]
        
        self.ax.legend(handles=legend_elements, loc='upper right', bbox_to_anchor=(1.15, 1))
        self.ax.set_title(f"Congestion Map - {layer}")
        self.ax.set_xlabel("X")
        self.ax.set_ylabel("Y")
        self.fig.tight_layout()


def main():
    parser = argparse.ArgumentParser(description="Visualize Global Routing Output & Congestion Map")
    parser.add_argument("input_file", help="Path to the original global routing input file (for exact pins)")
    parser.add_argument("routing_output", help="Path to the global routing output file")
    parser.add_argument("-c", "--congestion_map", help="Path to the congestion map file (optional)", default=None)
    args = parser.parse_args()
    
    grid_size, nets = parse_routing_file(args.routing_output)
    pins = parse_original_input_file(args.input_file)
    
    has_congestion = bool(args.congestion_map)
    cong_grid, cong_layers, cong_edges = parse_congestion_file(args.congestion_map)
    
    root = tk.Tk()
    app = VisualizerApp(root, grid_size, nets, cong_grid, cong_layers, cong_edges, has_congestion, pins)
    
    # 設定視窗關閉時結束程式
    root.protocol("WM_DELETE_WINDOW", root.quit)
    root.mainloop()

if __name__ == "__main__":
    main()
