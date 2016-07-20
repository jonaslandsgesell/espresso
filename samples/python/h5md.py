import espressomd
import sys
import numpy as np
from espressomd.h5md import H5md
from espressomd import integrate

system = espressomd.System()
system.box_l = [10.0, 10.0, 10.0]
system.time_step = 0.01
system.thermostat.set_langevin(kT=1.0, gamma=1.0)
system.skin = 0.4
for i in range(10):
    system.part.add(id=i, pos=np.random.random(3) * system.box_l, type=23)
h5md = H5md("data.h5")
integrate.integrate(1)
h5md.write_positions()
integrate.integrate(1)
h5md.write_velocities()
integrate.integrate(1)
h5md.write_forces()
h5md.close()