#!/usr/bin/env python3

import matplotlib.pyplot as plt
import sys
import os
import subprocess
import statistics
import pandas as pd
import fnmatch
plt.rcParams["font.family"] = "serif"
plt.rcParams['pdf.fonttype'] = 42
plt.rcParams["font.size"] = 8

interval = 1
pcap_file = sys.argv[1]
# assert pcap_file.startswith("sender_"), pcap_file
if pcap_file.startswith("pcaps/"):
	pcap_file = pcap_file[len("pcaps/"):]

is_secondary_flow = "port9010" in pcap_file
just_one_flow = "just_one_flow" in pcap_file

receiver_pcap = 'receiver_'+('_'.join(pcap_file.split('_')[1:]))

# new_rtt_command = f"tshark -r pcaps/{pcap_file} -Tfields -e frame.time_relative -e tcp.analysis.ack_rtt"
new_rtt_command = f"./wintracker pcaps/{pcap_file}"

# retransmissions_command = f"tshark -Y ip.src==192.168.0.1&&tcp.analysis.retransmission -r pcaps/{pcap_file} -Tfields -e frame.time_relative"
# packets_command = f"tshark -Y ip.src==192.168.0.1 -r pcaps/{receiver_pcap} -q -z io,stat,{interval},ip.src==192.168.0.1"

bytes_command = f"tshark -r pcaps/{receiver_pcap} -q -z io,stat,{interval},SUM(ip.len)ip.len&&ip.src==192.168.0.1"
bytes_command_total = f"tshark -r pcaps/{receiver_pcap} -q -z io,stat,0,SUM(ip.len)ip.len&&ip.src==192.168.0.1"

# rtt_out = subprocess.run(rtt_command.split(), check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
if "tcp" in pcap_file:
	rtt_out = subprocess.run(new_rtt_command, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)

	df = pd.read_csv(f"pcaps/{'.'.join(pcap_file.split('.')[:-1])}_full_1.csv")

	ack_timestamps = df['ackTimestamp'].tolist()
	rtts = [item*1000 for item in df['rtt'].tolist()]
else:
	timestamp = pcap_file.split("_")[-1].split(".")[0]
	for f in os.listdir('pcaps'):
		if fnmatch.fnmatch(f, f'*{timestamp}.txt'):
			rtt_text_file = f
			break
	# print("rtt_text_file", rtt_text_file)
	df = pd.read_csv(f"pcaps/{rtt_text_file}")

	ack_timestamps = df['timestamp'].tolist()
	min_timestamp = min(ack_timestamps)
	ack_timestamps = [ts-min_timestamp for ts in ack_timestamps]
	rtts = [item for item in df['RTT(ms)'].tolist()]

# print("ack_timestamps", ack_timestamps[:100])
# print("rtts", rtts[:100])
# rtt_out = [item.split("\t") for item in rtt_out.stdout.decode("utf-8").split("\n") if item!=""]
# # print("rtt_out", rtt_out)
# rtt_results = [(float(item[0]), 1000*float(item[1])) for item in rtt_out if item[1] != ""]
# print("average rtt", statistics.mean(list(zip(*rtt_results))[1]))
# print("rtt_results", rtt_results[:100])

# retransmissions_out = subprocess.run(retransmissions_command.split(), check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
# retransmissions_out = [item for item in retransmissions_out.stdout.decode("utf-8").split("\n") if item!=""]
# retransmissions_results = [float(item) for item in retransmissions_out]
# print("total retransmissions", len(retransmissions_results))

bytes_total_out = subprocess.run(bytes_command_total.split(), check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
bytes_total_out = bytes_total_out.stdout.decode("utf-8")
good_line = [line for line in bytes_total_out.split("\n") if "<>" in line][0]
# print(good_line.split("|"))
time_part, sum_part = good_line.split("|")[1:3]
# print(time_part.split("<>"))
start_time, end_time = [float(item.strip()) for item in time_part.split("<>")]
sum_part = int(sum_part.strip())/1000000*8
# print("start_time", start_time, "end_time", end_time, "sum_part", sum_part)
# print("bytes_total_out", bytes_total_out)

# packets_out = subprocess.run(packets_command.split(), check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
# packets_out = [item for item in packets_out.stdout.decode("utf-8").split("\n")[12:-3] if item!=""]
# packets_out = [[subitem for subitem in item.split("|") if subitem!=''] for item in packets_out]
# packets_out = [[item[0].split("<>"), *item[1:]] for item in packets_out]
# packets_results = [(float(item[0][0]), float(item[0][1]), float(item[1])) for item in packets_out]
# # print("packets_results", packets_results)

bytes_out = subprocess.run(bytes_command.split(), check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
# print("bytes_out", bytes_out.stdout.decode("utf-8"))
bytes_out = [item for item in bytes_out.stdout.decode("utf-8").split("\n") if item!="" and "<>" in item and not "Dur" in item]
# print("bytes_out", bytes_out)
bytes_out = [[subitem for subitem in item.split("|") if subitem!=''] for item in bytes_out]
bytes_out = [[item[0].split("<>"), *item[1:]] for item in bytes_out]
bytes_results = [(float(item[0][0]), float(item[0][1]), float(item[1])) for item in bytes_out]
# print("bytes_results", bytes_results)

# assert len(packets_results) == len(bytes_results)

# divided = [(first[0], first[1], first[2]/second[2]) for first, second in zip(bytes_results, packets_results)]
# print("divided", divided)

print("throughput", sum_part/(end_time-start_time))
print("avg_rtt", statistics.mean(rtts))

if len(sys.argv) > 2 and sys.argv[2] == "no_plotting":
	quit()

with_correct_time = [((item[0] + item[1])/2 - (5 if is_secondary_flow and not just_one_flow else 0), item[2]/1000000*8) for item in bytes_results]
os.makedirs("tex/plots", exist_ok=True)

plt.figure(figsize=(2.5,2))
plt.xlabel("Time [s]")
plt.ylabel(f"Throughput [Mbit/s]")
plt.plot(*zip(*[(a, b) for a, b in with_correct_time if a <= 20]))
plt.tight_layout()

plt.ylim(bottom=0)
plt.grid()
plt.savefig(f"plots/throughput_{interval}_{('_'.join(pcap_file.split('_')[1:]))}.pdf", bbox_inches = 'tight', pad_inches = 0)
# plt.savefig(f"plots/throughput_{interval}_{('_'.join(pcap_file.split('_')[1:]))}.png", bbox_inches = 'tight', pad_inches = 0, dpi=200)

plt.close()



plt.figure(figsize=(2.5,2))
plt.xlabel("Time [s]")
plt.ylabel(f"RTT [ms]")
# plt.plot(*zip(*rtt_results))

ack_timestamps = [ts - (5 if is_secondary_flow and not just_one_flow else 0) for ts in ack_timestamps]
plt.plot(*zip(*[(a, b) for a, b in zip(ack_timestamps, rtts) if a <= 20]))
# print("rtt_results", *zip(*rtt_results))
# plt.scatter(retransmissions_results, [0]*len(retransmissions_results), marker=".", linestyle="None", color="r", s=2, edgecolors="none")

plt.tight_layout()

plt.ylim(bottom=0)
plt.grid()
plt.savefig(f"plots/rtt_{interval}_{('_'.join(pcap_file.split('_')[1:]))}.pdf", bbox_inches = 'tight', pad_inches = 0)
# plt.savefig(f"plots/rtt_{interval}_{('_'.join(pcap_file.split('_')[1:]))}.png", bbox_inches = 'tight', pad_inches = 0, dpi=200)
# plt.show()

plt.close()