import re
import matplotlib.pyplot as plt

progName = "prog2"

file1 = open('addrtrace.out', 'r')  
totalAccGlobal, totalAccCache = 0, 0
distanceMapGlobal, distanceMapCache = {}, {}
while True:
    line = file1.readline()
    if not line:
        break
    if line.startswith("Global"):
        matches = re.findall("[\d\.]+", line)
        totalAccGlobal += int(matches[1])
        distanceMapGlobal[float(matches[0])] = totalAccGlobal
        # print("{}".format(line.strip()), float(matches[0]), distanceMapGlobal[float(matches[0])])
    elif line.startswith("Cache"):
        matches = re.findall("[\d\.]+", line)
        totalAccCache += int(matches[1])
        distanceMapCache[float(matches[0])] = totalAccCache
        # print("{}".format(line.strip()), float(matches[0]), distanceMapCache[float(matches[0])])

for key in distanceMapGlobal.keys():
    distanceMapGlobal[key] = distanceMapGlobal[key]/totalAccGlobal

for key in distanceMapCache.keys():
    distanceMapCache[key] = distanceMapCache[key]/totalAccCache

plt.figure(figsize=(10, 6))
plt.plot(distanceMapGlobal.keys(), distanceMapGlobal.values())
plt.title("cumulative density function for access distances (global)")
plt.grid(True)
plt.xlabel("Distances (Log scale)")
plt.ylabel("Number of accesses")
plt.savefig("CDF_{}_global".format(progName))

plt.plot(distanceMapCache.keys(), distanceMapCache.values())
plt.title("cumulative density function for access distances (both)")
plt.grid(True)
plt.savefig("CDF_{}_both".format(progName))

plt.clf()
plt.plot(distanceMapCache.keys(), distanceMapCache.values())
plt.title("cumulative density function for access distances (cache)")
plt.xlabel("Distances (Log scale)")
plt.ylabel("Number of accesses")
plt.grid(True)
plt.savefig("CDF_{}_cache".format(progName))