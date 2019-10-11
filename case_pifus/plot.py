#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np

mesh = 2
setup = False
#  display = True

num_source = [1000, 2000, 4000, 8000, 16000, 32000]

algos = ["ArborX", "ADT"]

data = np.ndarray([len(algos), len(num_source)])
if mesh == 1 and setup is True:
    data[0, :] = [0.024702, 0.111092, 0.227781, 1.023002, 2.368863, 10.145734]
    data[1, :] = [0.028676, 0.131406, 0.298287, 1.453377, 3.035177, 17.194715]
elif mesh == 1 and setup is False:
    data[0, :] = [0.035063, 0.043184, 0.299266, 0.371901, 2.862013, 3.099320]
    data[1, :] = [0.044752, 0.053131, 0.392653, 0.534305, 3.638508, 4.885098]
elif mesh == 2 and setup is True:
    data[0, :] = [0.003864, 0.004640, 0.049308, 0.046629, 0.475820, 0.418169]
    data[1, :] = [0.008703, 0.007500, 0.080427, 0.078363, 0.907204, 1.025535]
elif mesh == 2 and setup is False:
    data[0, :] = [0.021042, 0.093268, 0.217825, 0.887062, 1.846673, 8.330868]
    data[1, :] = [0.038551, 0.145837, 0.359765, 1.367968, 3.342207, 12.759598]

plt.figure(figsize=(6, 5))
ax = plt.subplot(111)

width = 0.6
scale = 1/len(algos)
for i in range(0, len(algos)):
    ax.bar(np.arange(len(num_source)) + scale*(i-1.5) * width,
           width=scale*width*np.ones(len(num_source)),
           height=data[i, :],
           edgecolor='k',
           label=algos[i])

plt.title('mesh = {}, setup = {}'.format(mesh, setup), fontsize=25)
ax.legend(loc='upper right', fontsize=20)

ax.set_xlabel('Scaling parameter', fontsize=20)
plt.xticks(fontsize=18)
ax.set_xticks(np.arange(len(num_source)))
#  ax.set_xticklabels(num_source_labels)
ax.tick_params(axis='x', which='major', bottom='off', top='off')
ax.set_xlim([-0.5, len(num_source)-0.5])

ax.set_ylabel('Time (s)', fontsize=20)
plt.yticks(fontsize=18)
ax.set_ylim([0, 1.7*data.max()])
ax.yaxis.grid('on')

plt.tight_layout()
plt.show()
