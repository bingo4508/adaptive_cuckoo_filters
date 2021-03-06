import re
import sys
import os

def get_filter_size(fname):
	try:
		with open(fname, 'r') as f:
			p = "Filter size: (\d+) bytes"
			return int(re.search(p, f.read()).group(1))
	except:
		return None

def get_FPP(fname):
	try:
		with open(fname, 'r') as f:
			p = "FPP \(%\): (.+)"
			return float(re.search(p, f.read()).group(1))
	except:
		return None

bits = int(sys.argv[1])
num_experiment = int(sys.argv[2])

input_dir = "output"
output_dir = "paper_result"

for i in range(1,10):
	if os.path.isfile("%s/%s_%d-%d-%d.result" % (input_dir, "acf", bits, i, 0)):
		print "==================== zipf 1.%d =========================" % (i)
		print "\tACF\tCF\tSBF\tBF"
		for j in range(num_experiment):
			if get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "bf", bits, i, j)):
				fpp_1 = get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "acf", bits, i, j))
				fpp_2 = get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "cf", bits, i, j))
				fpp_3 = get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "sbf", bits, i, j))
				fpp_4 = get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "bf", bits, i, j))
				print "%d\t%s\t%s\t%s\t%s" % (j, fpp_1, fpp_2, fpp_3, fpp_4)
		print "\n\tACF size\tCF size\tSBF size\tBF size"
		for j in range(num_experiment):
			if get_FPP("%s/%s_%d-%d-%d.result" % (input_dir, "bf", bits, i, j)):
				size_1 = get_filter_size("%s/%s_%d-%d-%d.result" % (input_dir, "acf", bits, i, j))
				size_2 = get_filter_size("%s/%s_%d-%d-%d.result" % (input_dir, "cf", bits, i, j))
				size_3 = get_filter_size("%s/%s_%d-%d-%d.result" % (input_dir, "sbf", bits, i, j))
				size_4 = get_filter_size("%s/%s_%d-%d-%d.result" % (input_dir, "bf", bits, i, j))
				print "%d\t%s\t%s\t%s\t%s" % (j, size_1, size_2, size_3, size_4)
		print "\n"
