#!/usr/bin/env python3

# usage: ./katara | python3 ./plot_pressure.py
# will pop a time series after you close out

import re
import sys
import matplotlib.pyplot as plt
from collections import deque

PATTERN = re.compile(r"min=([-\d.]+),\s*max=([-\d.]+)")
WINDOW_SIZE = 200
Y_PADDING = 0.1

def parse_line(line):
    match = PATTERN.search(line)
    if match:
        try:
            min_val = float(match.group(1))
            max_val = float(match.group(2))
            return min_val, max_val
        except ValueError:
            pass
    return None

def main():
    plt.style.use('dark_background')
    fig, ax = plt.subplots(figsize=(10, 6))
    fig.canvas.manager.set_window_title('under pressure')
    min_values = deque(maxlen=WINDOW_SIZE)
    max_values = deque(maxlen=WINDOW_SIZE)
    indices = deque(maxlen=WINDOW_SIZE)
    min_line, = ax.plot([], [], 'b-', label='Min', linewidth=1.5)
    max_line, = ax.plot([], [], 'r-', label='Max', linewidth=1.5)
    ax.legend(loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_xlabel('tick')
    ax.set_ylabel('pressure')
    counter = 0
    try:
        for line in sys.stdin:
            result = parse_line(line)
            if result is not None:
                min_val, max_val = result
                min_values.append(min_val)
                max_values.append(max_val)
                indices.append(counter)
                counter += 1
                min_line.set_data(indices, min_values)
                max_line.set_data(indices, max_values)
                if indices:
                    ax.set_xlim(min(indices), max(indices) + 1)
                if min_values or max_values:
                    all_values = list(min_values) + list(max_values)
                    y_min = min(all_values)
                    y_max = max(all_values)
                    y_range = y_max - y_min
                    if y_range == 0:
                        y_range = 1
                    ax.set_ylim(
                        y_min - y_range * Y_PADDING,
                        y_max + y_range * Y_PADDING
                    )
                fig.canvas.draw_idle()
                fig.canvas.flush_events()
        plt.show()
    except KeyboardInterrupt:
        print("\nplotting stopped by SIGINT.", file=sys.stderr)
    except Exception as e:
        print(f"ERR: {e}", file=sys.stderr)
        sys.exit(1)
    plt.show()

if __name__ == "__main__":
    main()
