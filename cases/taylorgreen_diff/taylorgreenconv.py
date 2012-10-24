from pylab import *
from taylorgreenfunc import *

t    = 2000
time = 0.1
visc = (8.*pi**2. / 10.)**(-1.)

ns    = array([16, 32, 64, 128])
dxs   = 1./ns

# 2nd order data
data16_2nd  = microhh(t,  16,   8, 'taylorgreen16_2nd' )
data32_2nd  = microhh(t,  32,  16, 'taylorgreen32_2nd' )
data64_2nd  = microhh(t,  64,  32, 'taylorgreen64_2nd' )
data128_2nd = microhh(t, 128,  64, 'taylorgreen128_2nd')

ref16_2nd  = getref(data16_2nd .x, data16_2nd .xh, data16_2nd .z, data16_2nd .zh, visc, time)
ref32_2nd  = getref(data32_2nd .x, data32_2nd .xh, data32_2nd .z, data32_2nd .zh, visc, time)
ref64_2nd  = getref(data64_2nd .x, data64_2nd .xh, data64_2nd .z, data64_2nd .zh, visc, time)
ref128_2nd = getref(data128_2nd.x, data128_2nd.xh, data128_2nd.z, data128_2nd.zh, visc, time)

err16_2nd  = geterror(data16_2nd , ref16_2nd )
err32_2nd  = geterror(data32_2nd , ref32_2nd )
err64_2nd  = geterror(data64_2nd , ref64_2nd )
err128_2nd = geterror(data128_2nd, ref128_2nd)

errsu_2nd = array([err16_2nd.u, err32_2nd.u, err64_2nd.u, err128_2nd.u])
errsw_2nd = array([err16_2nd.w, err32_2nd.w, err64_2nd.w, err128_2nd.w])
errsp_2nd = array([err16_2nd.p, err32_2nd.p, err64_2nd.p, err128_2nd.p])

print('errors p_2nd', errsp_2nd)
if(t > 0):
  print('convergence u_2nd', (log(errsu_2nd[-1])-log(errsu_2nd[0])) / (log(dxs[-1])-log(dxs[0])) )
  print('convergence w_2nd', (log(errsw_2nd[-1])-log(errsw_2nd[0])) / (log(dxs[-1])-log(dxs[0])) )
print('convergence p_2nd', (log(errsp_2nd[-1])-log(errsp_2nd[0])) / (log(dxs[-1])-log(dxs[0])) )

# 42 order data
data16_44  = microhh(t,  16,   8, 'taylorgreen16_44' )
data32_44  = microhh(t,  32,  16, 'taylorgreen32_44' )
data64_44  = microhh(t,  64,  32, 'taylorgreen64_44' )
data128_44 = microhh(t, 128,  64, 'taylorgreen128_44')

ref16_44  = getref(data16_44 .x, data16_44 .xh, data16_44 .z, data16_44 .zh, visc, time)
ref32_44  = getref(data32_44 .x, data32_44 .xh, data32_44 .z, data32_44 .zh, visc, time)
ref64_44  = getref(data64_44 .x, data64_44 .xh, data64_44 .z, data64_44 .zh, visc, time)
ref128_44 = getref(data128_44.x, data128_44.xh, data128_44.z, data128_44.zh, visc, time)

err16_44  = geterror(data16_44 , ref16_44 )
err32_44  = geterror(data32_44 , ref32_44 )
err64_44  = geterror(data64_44 , ref64_44 )
err128_44 = geterror(data128_44, ref128_44)

errsu_44 = array([err16_44.u, err32_44.u, err64_44.u, err128_44.u])
errsw_44 = array([err16_44.w, err32_44.w, err64_44.w, err128_44.w])
errsp_44 = array([err16_44.p, err32_44.p, err64_44.p, err128_44.p])

print('errors p_4thm', errsp_44)
if(t > 0):
  print('convergence u_4thm', (log(errsu_44[-1])-log(errsu_44[0])) / (log(dxs[-1])-log(dxs[0])) )
  print('convergence w_4thm', (log(errsw_44[-1])-log(errsw_44[0])) / (log(dxs[-1])-log(dxs[0])) )
print('convergence p_4thm', (log(errsp_44[-1])-log(errsp_44[0])) / (log(dxs[-1])-log(dxs[0])) )

# 4th order data
data16_4th  = microhh(t,  16,   8, 'taylorgreen16_4th' )
data32_4th  = microhh(t,  32,  16, 'taylorgreen32_4th' )
data64_4th  = microhh(t,  64,  32, 'taylorgreen64_4th' )
data128_4th = microhh(t, 128,  64, 'taylorgreen128_4th')

ref16_4th  = getref(data16_4th .x, data16_4th .xh, data16_4th .z, data16_4th .zh, visc, time)
ref32_4th  = getref(data32_4th .x, data32_4th .xh, data32_4th .z, data32_4th .zh, visc, time)
ref64_4th  = getref(data64_4th .x, data64_4th .xh, data64_4th .z, data64_4th .zh, visc, time)
ref128_4th = getref(data128_4th.x, data128_4th.xh, data128_4th.z, data128_4th.zh, visc, time)

err16_4th  = geterror(data16_4th , ref16_4th )
err32_4th  = geterror(data32_4th , ref32_4th )
err64_4th  = geterror(data64_4th , ref64_4th )
err128_4th = geterror(data128_4th, ref128_4th)

errsu_4th = array([err16_4th.u, err32_4th.u, err64_4th.u, err128_4th.u])
errsw_4th = array([err16_4th.w, err32_4th.w, err64_4th.w, err128_4th.w])
errsp_4th = array([err16_4th.p, err32_4th.p, err64_4th.p, err128_4th.p])

print('errors p_4th', errsp_4th)
if(t > 0):
  print('convergence u_4th', (log(errsu_4th[-1])-log(errsu_4th[0])) / (log(dxs[-1])-log(dxs[0])) )
  print('convergence w_4th', (log(errsw_4th[-1])-log(errsw_4th[0])) / (log(dxs[-1])-log(dxs[0])) )
print('convergence p_4th', (log(errsp_4th[-1])-log(errsp_4th[0])) / (log(dxs[-1])-log(dxs[0])) )

off2 = 0.01
off4 = 0.002
slope2 = off2*(dxs[:] / dxs[0])**2.
slope4 = off4*(dxs[:] / dxs[0])**4.

close('all')

figure()
if(t > 0):
  loglog(dxs, errsu_2nd, 'bo-', label="u_2nd")
  loglog(dxs, errsw_2nd, 'bv-', label="w_2nd")
  loglog(dxs, errsu_44 , 'go-', label="u_4thm" )
  loglog(dxs, errsw_44 , 'gv-', label="w_4thm" )
  loglog(dxs, errsu_4th, 'ro-', label="u_4th")
  loglog(dxs, errsw_4th, 'rv-', label="w_4th")
loglog(dxs, errsp_2nd, 'b^-', label="p_2nd")
loglog(dxs, errsp_44 , 'g^-', label="p_4thm" )
loglog(dxs, errsp_4th, 'r^-', label="p_4th")
loglog(dxs, slope2, 'k--', label="2nd")
loglog(dxs, slope4, 'k:' , label="4th")
legend(loc=0, frameon=False)
#xlim(0.01, 0.2)

"""
figure()
subplot(121)
pcolormesh(data128_44.xh, data128_44.z, data128_44.u[:,0,:])
xlim(min(data128_44.xh), max(data128_44.xh))
ylim(min(data128_44.z) , max(data128_44.z ))
xlabel('x')
ylabel('z')
title('u')
colorbar()
subplot(122)
pcolormesh(data128_44.xh, data128_44.z, ref128_44.u[:,0,:])
xlim(min(data128_44.xh), max(data128_44.xh))
ylim(min(data128_44.z ), max(data128_44.z ))
xlabel('x')
ylabel('z')
title('u ref')
colorbar()

figure()
subplot(121)
pcolormesh(data128_44.x, data128_44.zh, data128_44.w[:,0,:])
xlim(min(data128_44.x ), max(data128_44.x ))
ylim(min(data128_44.zh), max(data128_44.zh))
xlabel('x')
ylabel('z')
title('w')
colorbar()
subplot(122)
pcolormesh(data128_44.x, data128_44.zh, ref128_44.w[:,0,:])
xlim(min(data128_44.x ), max(data128_44.x ))
ylim(min(data128_44.zh), max(data128_44.zh))
xlabel('x')
ylabel('z')
title('w ref')
colorbar()

figure()
subplot(121)
pcolormesh(data128_44.x, data128_44.z, data128_44.p[:,0,:])
xlim(min(data128_44.x), max(data128_44.x))
ylim(min(data128_44.z), max(data128_44.z))
xlabel('x')
ylabel('z')
title('p')
colorbar()
subplot(122)
pcolormesh(data128_44.x, data128_44.z, ref128_44.p[:,0,:])
xlim(min(data128_44.x), max(data128_44.x))
ylim(min(data128_44.z), max(data128_44.z))
xlabel('x')
ylabel('z')
title('p ref')
colorbar()
"""

figure()
subplot(121)
pcolormesh(data128_2nd.x, data128_2nd.z, data128_2nd.u[:,0,:]-ref128_2nd.u[:,0,:])
xlim(min(data128_2nd.xh), max(data128_2nd.xh))
ylim(min(data128_2nd.z ), max(data128_2nd.z ))
xlabel('x')
ylabel('z')
title('u err_2nd')
colorbar()
subplot(122)
pcolormesh(data128_4th.x, data128_4th.z, data128_4th.u[:,0,:]-ref128_4th.u[:,0,:])
xlim(min(data128_4th.xh), max(data128_4th.xh))
ylim(min(data128_4th.z ), max(data128_4th.z ))
xlabel('x')
ylabel('z')
title('u err_4th')
colorbar()

figure()
subplot(121)
pcolormesh(data128_2nd.x, data128_2nd.z, data128_2nd.w[:,0,:]-ref128_2nd.w[:,0,:])
xlim(min(data128_2nd.xh), max(data128_2nd.xh))
ylim(min(data128_2nd.z ), max(data128_2nd.z ))
xlabel('x')
ylabel('z')
title('w err_2nd')
colorbar()
subplot(122)
pcolormesh(data128_4th.x, data128_4th.z, data128_4th.w[:,0,:]-ref128_4th.w[:,0,:])
xlim(min(data128_4th.x ), max(data128_4th.x ))
ylim(min(data128_4th.zh), max(data128_4th.zh))
xlabel('x')
ylabel('z')
title('w err_4th')
colorbar()

figure()
subplot(121)
pcolormesh(data128_2nd.x, data128_2nd.z, data128_2nd.p[:,0,:]-ref128_2nd.p[:,0,:])
xlim(min(data128_2nd.x), max(data128_2nd.x))
ylim(min(data128_2nd.z), max(data128_2nd.z))
xlabel('x')
ylabel('z')
title('p err_2nd')
colorbar()
subplot(122)
pcolormesh(data128_4th.x, data128_4th.z, data128_4th.p[:,0,:]-ref128_4th.p[:,0,:])
xlim(min(data128_4th.x), max(data128_4th.x))
ylim(min(data128_4th.z), max(data128_4th.z))
xlabel('x')
ylabel('z')
title('p err_4th')
colorbar()

