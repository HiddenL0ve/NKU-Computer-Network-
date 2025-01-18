import matplotlib.pyplot as plt
import seaborn as sns
import pandas as pd

# 数据
data = {
    "TestID": [1, 2, 3, 4, 5, 6],
    "Delay(ms)": [0, 0, 10, 10, 50, 50],
    "Loss(%)": [0, 0, 5, 5, 20, 20],
    "CongestionControl": ["Enabled", "Disabled", "Enabled", "Disabled", "Enabled", "Disabled"],
    "Throughput(MB/s)": [10.413, 6.72181, 0.0807916, 0.029358, 0.0123897, 0.0282262],
    "TransferTime(s)": [1.07442, 1.69813, 141.283, 388.805, 921.289, 404.395],
}

df = pd.DataFrame(data)

# 图1: 吞吐率对比
plt.figure(figsize=(10, 6))
sns.lineplot(x="Delay(ms)", y="Throughput(MB/s)", hue="CongestionControl", data=df, palette="Set2")
plt.yscale("log")  # 对数比例尺
plt.title("Comparison of Throughput with Congestion Control")
plt.xlabel("Delay (ms)")
plt.ylabel("Throughput (MB/s)")
plt.tight_layout()
plt.show()

