#!/usr/bin/env python3

import sys
import re
import matplotlib.pyplot as plt

def parse_log(filename):
    data = {
        'fps': [],
        'dropped_frames': [],
        'processing_time': [],
        'audio_latency': []
    }
    
    with open(filename, 'r') as f:
        for line in f:
            if 'Average FPS:' in line:
                data['fps'].append(float(re.findall(r'[\d.]+', line)[0]))
            elif 'Frame Processing Time' in line:
                data['processing_time'].append(float(re.findall(r'[\d.]+', line)[0]))
            elif 'Dropped Frames:' in line:
                data['dropped_frames'].append(int(re.findall(r'\d+', line)[0]))
            elif 'Audio Latency:' in line:
                data['audio_latency'].append(float(re.findall(r'[\d.]+', line)[0]))
    
    return data

def plot_comparison(mac_data, rpi_data):
    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(15, 10))
    
    # FPS
    ax1.plot(mac_data['fps'], label='Mac')
    ax1.plot(rpi_data['fps'], label='RPi 5')
    ax1.set_title('FPS Over Time')
    ax1.legend()
    
    # Autres métriques similaires...
    
    plt.tight_layout()
    plt.savefig('performance_comparison.png')

if __name__ == '__main__':
    mac_data = parse_log('mac_performance.log')
    rpi_data = parse_log('rpi_performance.log')
    plot_comparison(mac_data, rpi_data) 