#!/usr/bin/python
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from pylab import *
import matplotlib
import matplotlib.ticker as ticker
import os

def generate_plot(data, plot_path):
    (record_size, tp) = data
    fig, ax = plt.subplots(figsize=(6,4))
    xlabel = "Record Size (bytes)"
    ylabel = "Consumer\nThroughput (Mbps)"
    plt.plot(record_size, tp, '-ro')
    plt.ylabel(ylabel, fontsize=18)
    plt.xlabel(xlabel, fontsize=22)
    plt.xticks(fontsize=18)
    plt.yticks(fontsize=18)
    plt.xlim(left=0)
    #plt.ylim(bottom=0)
    #plt.legend(legend, fontsize =16, ncol=1)
    plt.xscale("log")
    plt.yscale("log")
    plt.tight_layout()
    pdf_page = PdfPages(plot_path)
    pdf_page.savefig()
    pdf_page.close()

def read_stats(data_path):
    with open(data_path) as f:
        content = f.readlines()
    content = content[1:]
    data = [float(x.strip().split(",")[0].strip()) for x in content]
    stats = [np.mean(data), np.percentile(data, 50), np.percentile(data, 99), np.percentile(data, 99.5), np.percentile(data, 99.9)]
    return stats

def read_data(data_dir):
    record_size = [16, 32, 96, 256, 512, 1024, 10240, 51240, 102400, 1024000]
    tp = [0.662279, 1.320513, 4.027352, 10.614479, 21.993128, 42.863121, 300.117223, 1059.8821, 1000.791642, 910.5783595]
    tp_bits = [8 * x for x in tp]
    return (record_size, tp_bits)

def bar_plot():
    data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "data", "nothing")
    plots_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "plots")
    plot_path = os.path.join(plots_dir, "micro_consumer_throughput.pdf")
    data = read_data(data_dir)
    generate_plot(data, plot_path)

def main():
    matplotlib.rcParams['ps.useafm'] = True
    matplotlib.rcParams['pdf.use14corefonts'] = True
    #matplotlib.rcParams['text.usetex'] = True
    matplotlib.rcParams.update({'figure.autolayout':True})
    matplotlib.rcParams.update({'font.size': 11})
    bar_plot()

if __name__ == "__main__":
    main()
