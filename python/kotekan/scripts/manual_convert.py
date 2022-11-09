import os
import glob
import time
import multiprocessing
import sys

PCO_ROOT = '/data/princeton/baseband/raw' # this is the output root
#CONFIG_PATH = '../../../config/chime_pco_gpu.yaml'
CONFIG_PATH = '../../../config/chime_pco_baseband_recv.j2'
def convert(fname):
    cmd = f"python baseband_archiver.py -c {CONFIG_PATH} {fname} --root {PCO_ROOT} --verbose"
    os.system(cmd)


num_threads = 10
data_dir = sys.argv[1] # this is the input root
if not os.path.exists(data_dir):
    print("ERROR!!! path not found. Enter a full path as an argument:")
    print("eg. /data/baseband_raw/baseband_raw_20211020160923")
files = glob.glob(os.path.join(data_dir,'baseband_*.data'))
files = [os.path.join(data_dir, f) for f in files]
t_global = time.time()
for i in range(0, len(files), num_threads):
    chunk = files[i : i + num_threads]
    threads = []
    manager = multiprocessing.Manager()
    for f in chunk:
        th = multiprocessing.Process(target=convert, args=(f,))
        th.start()
        time.sleep(1)
        threads.append(th)
    for th in threads:
        th.join()

print()
print("=================================")
# print ("Average time to convert eaeh file: ", sum(times)/len(times))
print("Total time to convert all files: ", time.time() - t_global)
