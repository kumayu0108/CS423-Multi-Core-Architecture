import re
import matplotlib.pyplot as plt

file1 = open('addrtrace.out', 'r')  
totalAcc = 0
distanceMap = {}
while True:
    line = file1.readline()
    if not line:
        break
    if not line.startswith("Access"):
        continue
    matches = re.findall("[\d\.]+", line)
    totalAcc += int(matches[1])
    distanceMap[float(matches[0])] = totalAcc
    print("{}".format(line.strip()), float(matches[0]), distanceMap[float(matches[0])])
plt.figure(figsize=(10, 6))
plt.plot(distanceMap.keys(), distanceMap.values())
plt.title("cumulative density function for access distances")
plt.grid(True)
plt.savefig("CDF_prog1")