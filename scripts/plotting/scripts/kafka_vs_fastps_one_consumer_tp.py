#!/usr/bin/python
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from pylab import *
import matplotlib
import matplotlib.ticker as ticker
import os

def generate_plot(data, plot_path, legend, x_ticks):
    (kafka, fastps, xlabel, ylabel) = data
    fig, ax = plt.subplots(figsize=(6,4))
    plot_group1_indices =[1, 1.75, 2.50, 3.25, 4.0]
    bar_width = 0.25
    opacity = 0.8
    plt.plot(x_ticks, kafka, '-ro', label="Kafka")
    plt.plot(x_ticks, fastps, '-bx', label="FastPS")
    plt.xlabel(xlabel, fontsize=24)
    plt.ylabel(ylabel, fontsize=24)
    ax.tick_params(axis = "x", pad=7)
    plt.xticks(fontsize=20)
    plt.yticks(fontsize=20)
    plt.legend(legend, loc='lower left', bbox_to_anchor= (0.2, 0.99), ncol=2, borderaxespad=0, frameon=False, fontsize=14)
    plt.ylim(top=35)
    plt.tight_layout()
    subplots_adjust(bottom=0.2, left=0.15)
    pdf_page = PdfPages(plot_path)
    #pp.savefig(bbox_inches='tight')
    pdf_page.savefig()
    pdf_page.close()

def bar_plot():
    data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "data")
    plots_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "plots")
    data_path = os.path.join(data_dir, "bar_plot.csv")
    plot_path = os.path.join(plots_dir, "kafka_vs_fastps_one_consumer_tp.pdf")
    x_ticks = [1, 2, 4, 6, 8]
    kafka = [26.66, 25.5, 20.94, 17.55, 13.63]
    fastps = [33.06, 32.7032, 33.9304, 32.6408, 31.7496]
    xlabel = "No. of Consumers"
    ylabel = "Throughput (Mbps)"
    data = (kafka, fastps, xlabel, ylabel)
    legend = ["Kafka", "FastPS"]
    generate_plot(data, plot_path, legend, x_ticks)

def main():
    matplotlib.rcParams['ps.useafm'] = True
    matplotlib.rcParams['pdf.use14corefonts'] = True
    #matplotlib.rcParams['text.usetex'] = True
    matplotlib.rcParams.update({'figure.autolayout':True})
    matplotlib.rcParams.update({'font.size': 11})
    bar_plot()

if __name__ == "__main__":
    main()
