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
    plot_group2_indices = [ i + bar_width for i in plot_group1_indices ]
    plot_group1 = plt.bar(plot_group1_indices, kafka, bar_width, label = legend, color = '#e41a1c', hatch = '//', edgecolor='black')
    plot_group2 = plt.bar(plot_group2_indices, fastps, bar_width, label = legend, color='#377eb8', hatch = '\\\\', edgecolor='black')
    xticks_indices = [ i + bar_width / 2 for i in plot_group1_indices]
    #plt.yscale('log', nonposy='clip')
    plt.xlabel(xlabel, fontsize=24)
    plt.ylabel(ylabel, fontsize=24)
    ax.tick_params(axis = "x", pad=7)
    plt.xticks(xticks_indices, x_ticks, fontsize=20)
    plt.yticks(fontsize=20)
    plt.legend(legend, loc='lower left', bbox_to_anchor= (0.2, 0.97), ncol=2, borderaxespad=0, frameon=False, fontsize=14)
    plt.ylim(top=260)
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
    plot_path = os.path.join(plots_dir, "kafka_vs_fastps_cumulative_tp.pdf")
    x_ticks = [1, 2, 4, 6, 8]
    kafka = [26.66, 51.32, 83.4, 103.99, 109.15]
    fastps = [33.06, 65.8544, 132.2368, 197.184, 251.7576]
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
