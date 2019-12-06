#!/usr/bin/python
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from pylab import *
import matplotlib
import matplotlib.ticker as ticker
import os

def generate_plot(data, plot_path):
    (num_consumers, cpu) = data
    fig, ax = plt.subplots(figsize=(6,4))
    xlabel = "No. of Consumers"
    ylabel = "Factor of increase"
    plot_group1_indices =[1, 1.5, 2.0, 2.5]
    bar_width = 0.25
    opacity = 0.8
    plot_group1 = plt.bar(plot_group1_indices, cpu, bar_width, color = '#e41a1c', hatch = '//', edgecolor='black')
    plt.ylabel(ylabel, fontsize=22)
    plt.xlabel(xlabel, fontsize=22)
    plt.xticks(plot_group1_indices, num_consumers, fontsize=18)
    plt.yticks(fontsize=18)
    plt.ylim(top=5)
    #plt.xlim(left=0)
    #plt.legend(legend, fontsize =16, ncol=1)
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
    num_consumers = [2, 4, 6, 8]
    cpu = [2.224489796, 2.469387755, 3.887755102, 4.142857143]
    return (num_consumers, cpu)

def bar_plot():
    data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "data", "nothing")
    plots_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "plots")
    plot_path = os.path.join(plots_dir, "peak_cpu_kafka.pdf")
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
