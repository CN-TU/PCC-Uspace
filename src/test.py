"""Example file for testing

This creates a small testnet with ipaddresses from 192.168.0.0/24,
one switch, and three hosts.
"""

import sys, os
import io
import time
import math
import signal
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(os.path.dirname(__file__))))

print("sys.version", sys.version)

start_time = int(time.time() * 1000)

import subprocess
# import matplotlib
# matplotlib.use("Agg")
# import matplotlib.pyplot as plt
import virtnet
import statistics
import argparse

# import numpy as np
# from scipy.stats import norm


# DELAY = 10000
# SIGMA = 2000
# SAMPLES = 1000
# X = 10
# NUMPING = 1000

parser = argparse.ArgumentParser()
parser.add_argument('--bytes_to_capture', type=int, default=100)
parser.add_argument('--delay', type=int, default=100)
parser.add_argument('--rate', type=float, default=8)
parser.add_argument('--time', type=float, default=10)
parser.add_argument('--qdisc', type=str, default="fq")
parser.add_argument('--cport', type=int, default=9000)
parser.add_argument('--buffer_size', type=int, default=10)
parser.add_argument('--how_many_values_per_parameter', type=int, default=5)
parser.add_argument('--run_scenario', type=str, default="")
parser.add_argument('--store_pcaps', action='store_true')
parser.add_argument('--competing_flow', action='store_true')

opt = parser.parse_args()
print(opt)



def run_commands(cmds, Popen=False):
	if type(cmds) is not list:
		cmds = [cmds]
	return_stuff = []
	for cmd in cmds:
		if type(cmd) is tuple:
			cmd, kwargs = cmd
		else:
			kwargs = {}
		try:
			print("cmd", cmd)#, "kwargs", kwargs)
			if not Popen:
				output = subprocess.run(cmd.split(" "), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True, **kwargs)
				# print("output", output)
				return_stuff.append(output)
			else:
				popen = subprocess.Popen(cmd.split(" "), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
				return_stuff.append(popen)
		except subprocess.CalledProcessError as e:
			print(e.cmd, e.returncode, e.output)
			raise e
	return return_stuff

# print("os.environ", os.environ)

def execute_popen_and_show_result(command, host=None):
	parent = host if host is not None else subprocess
	print(f"Executing{f' on host {host.name}' if host else ''}", command)
	with parent.Popen(command.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as cmd:
		out, err = cmd.stdout.read(), cmd.stderr.read()
		if out:
			print("out", out.decode("utf-8"))
		if err:
			print("err", err.decode("utf-8"))

number_of_seconds_the_competing_flow_starts_earlier = 5

def run(vnet):
		"Main functionality"

		# print("Calculating pdf...")

		# x = np.linspace(-X, X, SAMPLES)
		# y = norm.pdf(x, loc=-5)+norm.pdf(x, loc=5, scale=3)
		# area = np.trapz(y)*(2*X)/SAMPLES

		print("Building network...")
		network = vnet.Network("192.168.0.0/24")
		switch = vnet.Switch("sw")
		hosts = []
		for i in range(2):
			host = vnet.Host("host{}".format(i))
			host.connect(vnet.VirtualLink, switch, "eth0")
			# print("switch.interfaces", switch.interfaces)
			host["eth0"].add_ip(network)
			execute_popen_and_show_result("ethtool -K eth0 gro off", host)
			execute_popen_and_show_result("ethtool -K eth0 gso off", host)
			execute_popen_and_show_result("ethtool -K eth0 tso off", host)
			hosts.append(host)
			# print("host", host)
		# hosts[0]["eth0"].tc('add', 'netem', delay=DELAY, jitter=SIGMA, dist=y)

		# import pdb; pdb.set_trace()

		# print("switch.interfaces", switch.interfaces)
		for interface in switch.interfaces:
			print("interface", interface)
			# continue
			execute_popen_and_show_result(f"ethtool -K {interface} gro off")
			execute_popen_and_show_result(f"ethtool -K {interface} gso off")
			execute_popen_and_show_result(f"ethtool -K {interface} tso off")

			run_commands([f"tc qdisc add dev {interface} root handle 1: netem{f' delay {int(opt.delay)}ms' if interface=='host00' else ''}", f"tc qdisc add dev {interface} parent 1: handle 2: htb default 21", f"tc class add dev {interface} parent 2: classid 2:21 htb rate {opt.rate}mbit", f"tc qdisc add dev {interface} parent 2:21 handle 3: {opt.qdisc if interface=='host10' else 'fq'}{f' flow_limit {int(math.ceil(opt.buffer_size))}' if (interface=='host10' and opt.qdisc=='fq') else ''}{f' limit {int(math.ceil(opt.buffer_size))}' if (interface=='host10' and opt.qdisc=='pfifo') else ''}"])
		vnet.update_hosts()

		for i in range(len(hosts)):
			with hosts[i].Popen("tc qdisc show dev eth0".split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as qdisc_info:
				qdisc_info_output = qdisc_info.stdout.read().decode("utf-8").split("\n")
				print(f"qdisc_info_output host {i}", qdisc_info_output)

		with hosts[0].Popen("ping -c 100 -i 0 host1".split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as ping:
			ping_output = ping.stdout.read().decode("utf-8").split("\n")
			ping_output = [float(item.split()[-2][5:]) for item in ping_output if "time=" in item]
			mean_rtt = statistics.mean(ping_output)
			print("mean rtt", mean_rtt)
			assert mean_rtt >= opt.delay

		server_popen = hosts[1].Popen(f"./app/pccserver recv {opt.cport}".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		if opt.competing_flow:
			server_popen_iperf = hosts[1].Popen("iperf3 -V -4 -s".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)

		if opt.store_pcaps:
			os.makedirs("pcaps", exist_ok=True)
			tcpdump_sender_popen = hosts[0].Popen(f"/usr/sbin/tcpdump -s {opt.bytes_to_capture} -i eth0 -w pcaps/sender_{opt.qdisc}_{opt.delay}_{opt.rate}_{opt.time}_{start_time}.pcap dst port {opt.cport}".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			tcpdump_receiver_popen = hosts[1].Popen(f"/usr/sbin/tcpdump -s {opt.bytes_to_capture} -i eth0 -w pcaps/receiver_{opt.qdisc}_{opt.delay}_{opt.rate}_{opt.time}_{start_time}.pcap dst port {opt.cport}".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)

		# if opt.store_pcaps:
		# 	tcpdump_switch_popens = []
		# 	for interface_name in switch.interfaces.keys():
		# 		tcpdump_switch_popens.append(subprocess.Popen(f"/usr/sbin/tcpdump -s {opt.bytes_to_capture} -i {interface_name} -w pcaps/switch_{opt.qdisc}_{opt.delay}_{opt.rate}_{opt.time}_{start_time}_{interface_name}.pcap".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE))

		if opt.competing_flow:
			client_popen_iperf = hosts[0].Popen(f"iperf3 -V -4 -t {opt.time+number_of_seconds_the_competing_flow_starts_earlier} --cport {opt.cport+10} -c host1".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			time.sleep(number_of_seconds_the_competing_flow_starts_earlier)

		client_popen = hosts[0].Popen(f"./app/pccclient send host1 {opt.cport}".split(" "), stdout=subprocess.PIPE, stderr=subprocess.PIPE)

		time.sleep(opt.time)
		client_popen.terminate()
		out, err = client_popen.stdout.read(), client_popen.stderr.read()
		if out:
			print("client out", out.decode("utf-8"))
		if err:
			print("client err", err.decode("utf-8"))
		if opt.competing_flow:
			client_popen_iperf.terminate()
			out, err = client_popen_iperf.stdout.read(), client_popen_iperf.stderr.read()
			if out:
				print("client iperf out", out.decode("utf-8"))
			if err:
				print("client iperf err", err.decode("utf-8"))

		client_out = out

		server_popen.terminate()
		out, err = server_popen.stdout.read(), server_popen.stderr.read()
		if out:
			print("server out", out.decode("utf-8"))
		if err:
			print("server err", err.decode("utf-8"))

		if opt.competing_flow:
			server_popen_iperf.terminate()
			out, err = server_popen_iperf.stdout.read(), server_popen_iperf.stderr.read()
			if out:
				print("server iperf out", out.decode("utf-8"))
			if err:
				print("server iperf err", err.decode("utf-8"))

		if opt.store_pcaps:
			tcpdump_sender_popen.terminate()
			out, err = tcpdump_sender_popen.stdout.read(), tcpdump_sender_popen.stderr.read()
			if out:
				print("tcpdump out", out.decode("utf-8"))
			if err:
				print("tcpdump err", err.decode("utf-8"))

			tcpdump_receiver_popen.terminate()
			out, err = tcpdump_receiver_popen.stdout.read(), tcpdump_receiver_popen.stderr.read()
			if out:
				print("tcpdump out", out.decode("utf-8"))
			if err:
				print("tcpdump err", err.decode("utf-8"))

			subprocess.check_output("chmod -R o+rw pcaps".split())

		return client_out.decode("utf-8")

if opt.run_scenario == "":
	with virtnet.Manager() as context:
		run(context)
elif opt.run_scenario == "accuracy":
	import sklearn.metrics
	results_dict = {}
	for bw_index, bw in enumerate(np.linspace(1,10,opt.how_many_values_per_parameter)):
		for delay_index, delay in enumerate(np.linspace(10,100,opt.how_many_values_per_parameter)):
			for buffer_index, buffer in enumerate(np.linspace(5,50,opt.how_many_values_per_parameter)):
				for fq_index, fq in enumerate([False, True]):
					opt.rate = int(round(bw))
					opt.delay = int(round(delay))
					opt.buffer_size = int(round(buffer))
					opt.qdisc = "fq" if fq else "pfifo"
					opt.time = 5

					with virtnet.Manager() as context:
						client_output = run(context)
					contained_vegas = "Starting Vegas" in client_output
					contained_pcc = "Starting PCC Classic" in client_output

					results_dict[(bw, delay, buffer, fq)] = (contained_vegas, contained_pcc)

	invalid = 0
	predictions = []
	for (bw, delay, buffer, fq), (is_vegas, is_pcc) in results_dict.items():
		is_invalid = (not is_vegas and not is_pcc)
		invalid += is_invalid
		if not is_invalid:
			predictions.append((fq, is_vegas))

	print("invalid", invalid, "total", len(results_dict))#, "results", results_dict)
	confusion_matrix_input = list(zip(*predictions))
	# print("confusion_matrix_input", confusion_matrix_input)
	accuracy_score = sklearn.metrics.accuracy_score(*confusion_matrix_input)
	print("accuracy_score", accuracy_score)
	confusion_matrix = sklearn.metrics.confusion_matrix(*confusion_matrix_input)
	print("confusion_matrix", confusion_matrix)

elif opt.run_scenario == "competing_flow":
	opt.competing_flow = True

	opt.time = 60
	opt.store_pcaps = True

	print("Starting fq experiment")
	opt.qdisc = "fq"
	with virtnet.Manager() as context:
		client_output = run(context)

	print("Starting pfifo experiment")
	opt.qdisc = "pfifo"
	with virtnet.Manager() as context:
		client_output = run(context)

	del os.environ["START_VEGAS"]
	del os.environ["START_PCC_CLASSIC"]
	os.environ["START_PCC_CLASSIC"] = "1"
	print("Starting fq experiment with PCC_CLASSIC")
	opt.qdisc = "fq"
	with virtnet.Manager() as context:
		client_output = run(context)

	del os.environ["START_VEGAS"]
	del os.environ["START_PCC_CLASSIC"]
	os.environ["START_VEGAS"] = "1"
	opt.qdisc = "pfifo"
	print("Starting pfifo experiment with VEGAS")
	with virtnet.Manager() as context:
		client_output = run(context)

