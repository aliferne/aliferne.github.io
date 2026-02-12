---
title: EndeavourOS 上英伟达显卡配置流程
date: 2026-02-09 13:42:34
categories:
- linux
tags:
- linux
- archlinux
- endeavourOS
- nvidia
---

## 配置流程

二更：刚发现 EndeavourOS 有提供 `nvidia-inst` 命令一键安装，真是亏大了：

```bash
yay -S nvidia-inst
nvidia-inst
```

此外 `optimus-manager` 也就没必要安装了，反正都切不了显卡，用上面的命令能正常带动就这样就行了。

郑重提醒：为了避免你设置完之后整个电脑桌面环境炸掉还不知道怎么退档，强烈建议你先装 `timeshift` 然后先保存一份快照，操作有风险，谨慎执行下面的命令，在没有搞清楚命令是干什么的之前都尽量不要执行，此外也不要完全信任 AI 给你的 “最终解决方案”，不然很有可能这一站将会是你 Linux 生涯的终点站。

先说下我个人电脑情况，我的电脑是华硕天选 5 Pro：

![fastfecth-result](./images/fastfetch-result.png)

我更习惯 Wayland 多一点，用 X11 的时候总觉得中文字体有点糊，不过我下面遇到的使用独显直接渲染或者混合模式渲染会导致桌面卡死的问题，则是在 X11 和 Wayland 上都有。

下面是使用 `inxi -Gx` 命令跑的，建议装一下这个，虽然 `lspci` 也可以显示硬件信息，但我个人觉得这个更直观些：

```bash
sudo pacman -S inxi
```

```bash
inxi -Gx
```

输出如下：

```bash
$ inxi -Gx
Graphics:
  Device-1: NVIDIA AD107M [GeForce RTX 4060 Max-Q / Mobile] vendor: ASUSTeK
    driver: nvidia v: 590.48.01 arch: Lovelace bus-ID: 01:00.0
  Device-2: Advanced Micro Devices [AMD/ATI] Raphael vendor: ASUSTeK
    driver: amdgpu v: kernel arch: RDNA-2 bus-ID: 06:00.0 temp: 56.0 C
  Device-3: Sonix USB2.0 HD UVC WebCam driver: uvcvideo type: USB
    bus-ID: 1-1:2
  Display: wayland server: X.org v: 1.21.1.21 with: Xwayland v: 24.1.9
    compositor: kwin_wayland driver: X: loaded: modesetting dri: radeonsi
    gpu: amdgpu resolution: 2560x1600~165Hz
  API: EGL v: 1.5 drivers: kms_swrast,nvidia,radeonsi,swrast platforms:
    active: gbm,wayland,x11,surfaceless,device inactive: N/A
  API: OpenGL v: 4.6.0 compat-v: 4.5 vendor: nvidia mesa v: 25.3.4-arch1.1
    glx-v: 1.4 direct-render: yes renderer: NVIDIA GeForce RTX 4060 Laptop
    GPU/PCIe/SSE2
  API: Vulkan v: 1.4.335 drivers: radv,nvidia surfaces: N/A devices: 2
  Info: Tools: api: clinfo, eglinfo, glxinfo, vulkaninfo
    de: kscreen-console,kscreen-doctor gpu: nvidia-settings, nvidia-smi,
    radeontop wl: wayland-info x11: xdpyinfo, xprop, xrandr
```

在 [ArchWiki][ArchWiki] 里面给的是这个命令：

```bash
$ lspci -k -d ::03xx
01:00.0 VGA compatible controller: NVIDIA Corporation AD107M [GeForce RTX 4060 Max-Q / Mobile] (rev a1)
        Subsystem: ASUSTeK Computer Inc. Device 3638
        Kernel modules: nouveau
06:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Raphael (rev d8)
        Subsystem: ASUSTeK Computer Inc. Device 3638
        Kernel driver in use: amdgpu
        Kernel modules: amdgpu
```

重点关注 `AD107M` 和 `RTX 4060`，型号是我们要对应装哪个驱动用的。此外这里有个 nouveau，如果你的电脑也装了 nouveau，记得禁用！

禁用 nouveau 的方法：

```bash
lsmod | grep nouveau  # 若输出结果，说明 nouveau 正在运行
```

```bash
sudo tee /etc/modprobe.d/blacklist-nouveau.conf <<EOF
blacklist nouveau
options nouveau modeset=0
EOF
```

[链接][ArchWiki] 里面有说明对应的显卡型号，在 Arch Wiki 里面对应如下的驱动：

- nvidia-open
- nvidia-580xx-dkms ( AUR 打包，但使用 `yay -S` 时提示 403 )

由于标注是两者都兼容，那么我们选择 nvidia-open

注：我发现 nvidia-open 似乎无法使用，`nvidia-smi` 无论如何都不成功，查阅 [这篇博客][ArchGuide] 时发现里面提到 `nvidia-open` 不能很好地兼容我这种英伟达 + AMD核显的电脑，因此其实只能装 `nvidia-open-dkms`

而安装 dkms 的话需要参考该链接：

https://wiki.archlinux.org/title/Dynamic_Kernel_Module_Support#Installation

里面提到了安装 dkms 需要确保有 `linux-headers`，这个 `linux-headers` 是对应的头文件依赖，需要先查看你的内核：

```bash
$ uname -r
6.18.7-zen1-1-zen
```

对于我 EOS 来说，我需要安装的就是 `linux-zen-headers`

我安装的内容如下：

```bash
sudo pacman -S lib32-nvidia-utils nvidia-open-dkms nvidia-settings nvidia-utils nvidia-prime linux-zen-headers
```

```bash
$ pacman -Q | grep nvidia-
lib32-nvidia-utils 590.48.01-1
nvidia-open-dkms 590.48.01-2
nvidia-prime 1.0-5
nvidia-settings 590.48.01-1
nvidia-utils 590.48.01-2
```

lib32 是为了支持 32 位的应用程序的，建议安装。

然后我们还需安装 `optimus-manager`，这个是用来管理混合显卡的：

```bash
yay -S optimus-manager optimus-manager-qt
```

装完之后需要重启才能启动 optimus-manager-qt，此外我遇到的一个问题是：

如果使用英伟达独显或者混合模式启动电脑，则电脑无法正常进入 SDDM 或者在 KDE 桌面运行一会后卡死，但是只使用 AMD 核显则可以正常运作，尚不清除是什么原因

解决方法（在 SDDM 界面中先别登录，需要先唤出 tty 界面）：

```bash
optimus-manager --switch integrated # 切换为核显
optimus-manage --status # 查看是否切换到了核显
```

输出示例：

```
Version: 813

Current mode: integrated
Mode for next login: Current
Startup mode: integrated
Temporary config: None
```

关注点是 `Current mode` 和  `Startup mode`，如果这两个都是 integrated 那就 OK，如果有一个不是那就要注意了：

- 当前模式如果不是核显，那我这里桌面会卡死
- 启动模式如果不是核显，那么下次启动（如果你现在是核显模式的话）就会切换到对应模式，那么也会卡死

然后再重启一下桌面环境：

```bash
sudo systemctl restart display-manager
```

登录进桌面之后记得设置一下 optimus-manager，这个时候用图形界面也可以了，记得启动模式选核显。

因为 Wayland 要求 DRM 启用，而 nvidia-utils 560.35.03-5 版本开始，DRM 默认启用（参见 [ArchWiki][ArchWiki]）。因此如果你用 Wayland 且 nvidia-utils 版本高于这个版本，那应该不用管，验证启用的方法就是：

```bash
sudo cat /sys/module/nvidia_drm/parameters/modeset
```

如果启用了会输出 Y 。但我的电脑会提示没有这个文件，我推测可能这个是导致我使用 4060 直接渲染会导致桌面卡死的原因，不过没找到管用的解决方法。

对于一些不再支持的旧驱动程序， fbdev 也应当设置。这里就不讲了（我似乎没这种需求）

下面的这个命令将告诉你已经有的显卡和当前正在使用的显卡（标注 in use 的）

```bash
$ lspci -k | grep -A3 -E "(VGA|3D)" 
01:00.0 VGA compatible controller: NVIDIA Corporation AD107M [GeForce RTX 4060 Max-Q / Mobile] (rev a1)
        Subsystem: ASUSTeK Computer Inc. Device 3638
        Kernel modules: nouveau
01:00.1 Audio device: NVIDIA Corporation AD107 High Definition Audio Controller (rev a1)
--
06:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Raphael (rev d8)
        Subsystem: ASUSTeK Computer Inc. Device 3638
        Kernel driver in use: amdgpu
        Kernel modules: amdgpu
```

而下面这个命令则会显示当前已经加载出内核模块的显卡：

```bash
$ lsmod | grep -E 'amdgpu|radeon|nvidia'
nvidia_modeset       2121728  5
nvidia_uvm           2662400  0
nvidia              16322560  204 nvidia_uvm,nvidia_modeset
amdgpu              16781312  76
amdxcp                 12288  1 amdgpu
drm_panel_backlight_quirks    12288  1 amdgpu
gpu_sched              73728  1 amdgpu
drm_buddy              28672  1 amdgpu
drm_exec               12288  1 amdgpu
drm_suballoc_helper    20480  1 amdgpu
drm_ttm_helper         16384  2 amdgpu
ttm                   135168  2 amdgpu,drm_ttm_helper
i2c_algo_bit           24576  1 amdgpu
drm_display_helper    303104  1 amdgpu
cec                   110592  2 drm_display_helper,amdgpu
nvidia_wmi_ec_backlight    12288  0
video                  81920  5 nvidia_wmi_ec_backlight,asus_wmi,amdgpu,asus_nb_wmi,nvidia_modeset
wmi                    36864  4 video,nvidia_wmi_ec_backlight,asus_wmi,wmi_bmof
```

此外我看到很多博客以及 AI 都提到了这个修改 nvidia-drm 的方法，我在这里说一下，这个方法大概率没用（至少在我的电脑上）：

```bash
sudo vim /etc/default/grub
# 找到 `GRUB_CMDLINE_LINUX_DEFAULT`，在值后面追加 `nvidia-drm.modeset=1`
# 保存退出，跑下面的命令
# 让配置生效
sudo grub-mkconfig -o /boot/grub/grub.cfg
```

也就是下面的 [静夜博客][Zido-Blog] 是不应该参考的（时间也确实比较久远了），如果有大佬知道为啥的话欢迎交流经验。

因此现在默认情况下是核显在渲染，但是独显你也可以调用，只要使用 `prime-run` 就行了（我们之前装的 `nvidia-prime` 此时就派上用场了）

```bash
prime-run glxgear &
```

然后正常来说你会见到齿轮在转圈，比如我这么操作：

```bash
$ prime-run glxgears > log & # 这个是把调试信息重定向到 log 中，你也可以用上面的命令，缺点就是会不时在终端打印日志
[1] 21283

$ nvidia-smi                
Mon Feb  9 13:04:57 2026       
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 590.48.01              Driver Version: 590.48.01      CUDA Version: 13.1     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
|=========================================+========================+======================|
|   0  NVIDIA GeForce RTX 4060 ...    Off |   00000000:01:00.0 Off |                  N/A |
| N/A   54C    P0             19W /   55W |      24MiB /   8188MiB |     38%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------+------------------------+----------------------+

+-----------------------------------------------------------------------------------------+
| Processes:                                                                              |
|  GPU   GI   CI              PID   Type   Process name                        GPU Memory |
|        ID   ID                                                               Usage      |
|=========================================================================================|
|    0   N/A  N/A            2130      G   /usr/bin/ksmserver                        2MiB |
|    0   N/A  N/A            2300      G   /usr/bin/kaccess                          2MiB |
|    0   N/A  N/A           21285      G   glxgears                                  4MiB | # 这里可以看到有 glxgears，那么就表示确实调用了英伟达显卡
+-----------------------------------------------------------------------------------------+
```

## 目前我仍然存在的问题

为了避免干扰整体阅读的流畅程度，我把我遇到的问题在这里贴出来

现在唯一的问题就是 dmesg 里面显示 AMD 核显卡和 Nvidia 独显卡加载是成功的

### dmesg 内容

```bash
$ sudo dmesg | grep -i -E "(nvidia|amd)"
[    0.000000] Command line: BOOT_IMAGE=/@/boot/vmlinuz-linux-zen root=UUID=5df5effb-4788-4fa9-b960-794a6556c0f4 rw rootflags=subvol=@ nowatchdog nvme_load=YES resume=UUID=709c3e6c-06cc-406c-904a-3cd8a241b159 loglevel=3 nvidia-drm.modeset=1
[    0.003101] RAMDISK: [mem 0x70c32000-0x77b88fff]
[    0.003123] ACPI: IVRS 0x0000000094CDA000 0001A4 (v02 AMD    AmdTable 00000001 AMD  00000001)
[    0.003124] ACPI: SSDT 0x0000000094CD2000 007F51 (v02 AMD    Vortex   00000002 MSFT 05000000)
[    0.003130] ACPI: VFCT 0x0000000094CB0000 00AE84 (v01 _ASUS_ Notebook 00000001 AMD  33504F47)
[    0.003132] ACPI: SSDT 0x0000000094CA4000 009BAE (v02 AMD    AmdTable 00000001 AMD  00000001)
[    0.003133] ACPI: CRAT 0x0000000094CA2000 001D28 (v01 AMD    AmdTable 00000001 AMD  00000001)
[    0.003135] ACPI: CDIT 0x0000000094CA1000 000029 (v01 AMD    AmdTable 00000001 AMD  00000001)
[    0.003137] ACPI: SSDT 0x0000000094C9B000 0006B5 (v02 AMD    CPMWLRC  00000001 INTL 20230331)
[    0.003139] ACPI: SSDT 0x0000000094C99000 0015B8 (v02 AMD    CPMDFIG2 00000001 INTL 20230331)
[    0.003139] ACPI: SSDT 0x0000000094C96000 0029DC (v02 AMD    CDFAAIG2 00000001 INTL 20230331)
[    0.003140] ACPI: SSDT 0x0000000094C8A000 009713 (v02 AMD    CPMCMN   00000001 INTL 20230331)
[    0.003141] ACPI: SSDT 0x0000000094C87000 0022AD (v02 AMD    AOD      00000001 INTL 20230331)
[    0.003142] ACPI: SSDT 0x0000000094C95000 000EA2 (v02 AMD    NVME     00000001 INTL 20230331)
[    0.003143] ACPI: SSDT 0x0000000094C94000 000EA2 (v02 AMD    NVME     00000001 INTL 20230331)
[    0.003146] ACPI: SSDT 0x0000000094C83000 001BAF (v02 AMD    GPP_PME_ 00000001 INTL 20230331)
[    0.003147] ACPI: SSDT 0x0000000094C82000 0006DA (v02 AMD    EXTGPP00 00000001 INTL 20230331)
[    0.003148] ACPI: SSDT 0x0000000094C81000 000952 (v02 AMD    CPMMSOSC 00000001 INTL 20230331)
[    0.003149] ACPI: SSDT 0x0000000094C80000 000464 (v02 AMD    AMDWOV   00000001 INTL 20230331)
[    0.003150] ACPI: SSDT 0x0000000094C7F000 000E06 (v02 AMD    CPMUCSI  00000001 INTL 20230331)
[    0.003151] ACPI: SSDT 0x0000000094C7E000 00008D (v02 AMD    CPMMSLPI 00000001 INTL 20230331)
[    0.003152] ACPI: SSDT 0x0000000094C7D000 00094C (v02 AMD    TZ01     00000001 INTL 20230331)
[    0.003153] ACPI: SSDT 0x0000000094C7C000 00066F (v02 AMD    XHCI     00000001 INTL 20230331)
[    0.003154] ACPI: SSDT 0x0000000094C7B000 00070D (v02 AMD    GPIORPL  00000001 INTL 20230331)
[    0.003155] ACPI: SSDT 0x0000000094C79000 001016 (v02 AMD    UPEPRPL  00000001 INTL 20230331)
[    0.003156] ACPI: SSDT 0x0000000094C78000 00067B (v02 AMD    GPPRPL   00000001 INTL 20230331)
[    0.003157] ACPI: SSDT 0x0000000094C76000 00102E (v02 AMD    CPMACPV7 00000001 INTL 20230331)
[    0.060847] Kernel command line: BOOT_IMAGE=/@/boot/vmlinuz-linux-zen root=UUID=5df5effb-4788-4fa9-b960-794a6556c0f4 rw rootflags=subvol=@ nowatchdog nvme_load=YES resume=UUID=709c3e6c-06cc-406c-904a-3cd8a241b159 loglevel=3 nvidia-drm.modeset=1
[    0.142250] AMD-Vi: ivrs, add hid:AMDI0020, uid:\_SB.FUR0, rdevid:0xa0
[    0.142251] AMD-Vi: ivrs, add hid:AMDI0020, uid:\_SB.FUR1, rdevid:0xa0
[    0.142252] AMD-Vi: ivrs, add hid:AMDI0020, uid:\_SB.FUR2, rdevid:0xa0
[    0.142252] AMD-Vi: ivrs, add hid:AMDI0020, uid:\_SB.FUR3, rdevid:0xa0
[    0.142252] AMD-Vi: Using global IVHD EFR:0x246577efa2254afa, EFR2:0x0
[    0.275802] smpboot: CPU0: AMD Ryzen 9 7940HX with Radeon Graphics (family: 0x19, model: 0x61, stepping: 0x2)
[    0.275906] Performance Events: Fam17h+ 16-deep LBR, core perfctr, AMD PMU driver.
[    0.422358] pci 0000:00:00.2: AMD-Vi: IOMMU performance counters supported
[    0.425348] AMD-Vi: Extended features (0x246577efa2254afa, 0x0): PPR NX GT [5] IA GA PC GA_vAPIC
[    0.425355] AMD-Vi: Interrupt remapping enabled
[    0.529957] AMD-Vi: Virtual APIC enabled
[    0.530865] perf: AMD IBS detected (0x00000bff)
[    0.530871] perf/amd_iommu: Detected AMD IOMMU #0 (2 banks, 4 counters/bank).
[    0.650362] x86/amd: Previous system reset reason [0x00080800]: software wrote 0x6 to reset control register 0xCF9
[    0.952834] kvm_amd: TSC scaling supported
[    0.952836] kvm_amd: Nested Virtualization enabled
[    0.952837] kvm_amd: Nested Paging enabled
[    0.952838] kvm_amd: LBR virtualization supported
[    0.952840] kvm_amd: AVIC enabled
[    0.952840] kvm_amd: x2AVIC enabled
[    0.952841] kvm_amd: Virtual GIF supported
[    0.952842] kvm_amd: Virtual NMI enabled
[    1.301335] input: ITE5570:00 0B05:19B6 Wireless Radio Control as /devices/platform/AMDI0010:03/i2c-2/i2c-ITE5570:00/0018:0B05:19B6.0001/input/input6
[    1.302974] input: ASUF1204:00 2808:0202 Mouse as /devices/platform/AMDI0010:00/i2c-0/i2c-ASUF1204:00/0018:2808:0202.0002/input/input7
[    1.303051] input: ASUF1204:00 2808:0202 Touchpad as /devices/platform/AMDI0010:00/i2c-0/i2c-ASUF1204:00/0018:2808:0202.0002/input/input8
[    1.347916] input: ASUF1204:00 2808:0202 Mouse as /devices/platform/AMDI0010:00/i2c-0/i2c-ASUF1204:00/0018:2808:0202.0002/input/input11
[    1.347964] input: ASUF1204:00 2808:0202 Touchpad as /devices/platform/AMDI0010:00/i2c-0/i2c-ASUF1204:00/0018:2808:0202.0002/input/input12
[    3.687188] amd_atl: AMD Address Translation Library initialized
[    3.837804] input: HDA NVidia HDMI/DP,pcm=3 as /devices/pci0000:00/0000:00:01.1/0000:01:00.1/sound/card1/input27
[    3.837846] input: HDA NVidia HDMI/DP,pcm=7 as /devices/pci0000:00/0000:00:01.1/0000:01:00.1/sound/card1/input28
[    3.837888] input: HDA NVidia HDMI/DP,pcm=8 as /devices/pci0000:00/0000:00:01.1/0000:01:00.1/sound/card1/input29
[    3.837920] input: HDA NVidia HDMI/DP,pcm=9 as /devices/pci0000:00/0000:00:01.1/0000:01:00.1/sound/card1/input30
[    5.150494] [drm] amdgpu kernel modesetting enabled.
[    5.150515] amdgpu: vga_switcheroo: detected switching method \_SB_.PCI0.GP17.VGA_.ATPX handle
[    5.150661] amdgpu: ATPX version 1, functions 0x00000001
[    5.150763] amdgpu: ATPX Hybrid Graphics
[    5.159580] amdgpu: Virtual CRAT table created for CPU
[    5.159590] amdgpu: Topology: Add CPU node
[    5.159683] amdgpu 0000:06:00.0: enabling device (0006 -> 0007)
[    5.159720] amdgpu 0000:06:00.0: amdgpu: initializing kernel modesetting (IP DISCOVERY 0x1002:0x164E 0x1043:0x3638 0xD8).
[    5.159960] amdgpu 0000:06:00.0: amdgpu: register mmio base: 0xFC500000
[    5.159961] amdgpu 0000:06:00.0: amdgpu: register mmio size: 524288
[    5.161638] amdgpu 0000:06:00.0: amdgpu: detected ip block number 0 <common_v1_0_0> (nv_common)
[    5.161641] amdgpu 0000:06:00.0: amdgpu: detected ip block number 1 <gmc_v10_0_0> (gmc_v10_0)
[    5.161643] amdgpu 0000:06:00.0: amdgpu: detected ip block number 2 <ih_v5_0_0> (navi10_ih)
[    5.161645] amdgpu 0000:06:00.0: amdgpu: detected ip block number 3 <psp_v13_0_0> (psp)
[    5.161646] amdgpu 0000:06:00.0: amdgpu: detected ip block number 4 <smu_v13_0_0> (smu)
[    5.161648] amdgpu 0000:06:00.0: amdgpu: detected ip block number 5 <dce_v1_0_0> (dm)
[    5.161649] amdgpu 0000:06:00.0: amdgpu: detected ip block number 6 <gfx_v10_0_0> (gfx_v10_0)
[    5.161651] amdgpu 0000:06:00.0: amdgpu: detected ip block number 7 <sdma_v5_2_0> (sdma_v5_2)
[    5.161653] amdgpu 0000:06:00.0: amdgpu: detected ip block number 8 <vcn_v3_0_0> (vcn_v3_0)
[    5.161654] amdgpu 0000:06:00.0: amdgpu: detected ip block number 9 <jpeg_v3_0_0> (jpeg_v3_0)
[    5.161672] amdgpu 0000:06:00.0: amdgpu: Fetched VBIOS from VFCT
[    5.161673] amdgpu: ATOM BIOS: 102-RAPHAEL-008
[    5.211763] amdgpu 0000:06:00.0: vgaarb: deactivate vga console
[    5.211765] amdgpu 0000:06:00.0: amdgpu: Trusted Memory Zone (TMZ) feature disabled as experimental (default)
[    5.211797] amdgpu 0000:06:00.0: amdgpu: vm size is 262144 GB, 4 levels, block size is 9-bit, fragment size is 9-bit
[    5.211802] amdgpu 0000:06:00.0: amdgpu: VRAM: 512M 0x000000F400000000 - 0x000000F41FFFFFFF (512M used)
[    5.211804] amdgpu 0000:06:00.0: amdgpu: GART: 1024M 0x0000000000000000 - 0x000000003FFFFFFF
[    5.211900] amdgpu 0000:06:00.0: amdgpu: amdgpu: 512M of VRAM memory ready
[    5.211902] amdgpu 0000:06:00.0: amdgpu: amdgpu: 19651M of GTT memory ready.
[    5.212288] amdgpu 0000:06:00.0: amdgpu: [drm] Loading DMUB firmware via PSP: version=0x05002E00
[    5.212615] amdgpu 0000:06:00.0: amdgpu: [VCN instance 0] Found VCN firmware Version ENC: 1.33 DEC: 4 VEP: 0 Revision: 14
[    5.234781] amdgpu 0000:06:00.0: amdgpu: reserve 0xa00000 from 0xf41e000000 for PSP TMR
[    5.299494] amdgpu 0000:06:00.0: amdgpu: RAS: optional ras ta ucode is not available
[    5.305578] amdgpu 0000:06:00.0: amdgpu: RAP: optional rap ta ucode is not available
[    5.305581] amdgpu 0000:06:00.0: amdgpu: SECUREDISPLAY: optional securedisplay ta ucode is not available
[    5.306937] amdgpu 0000:06:00.0: amdgpu: SMU is initialized successfully!
[    5.307910] amdgpu 0000:06:00.0: amdgpu: [drm] Display Core v3.2.351 initialized on DCN 3.1.5
[    5.307913] amdgpu 0000:06:00.0: amdgpu: [drm] DP-HDMI FRL PCON supported
[    5.308742] amdgpu 0000:06:00.0: amdgpu: [drm] DMUB hardware initialized: version=0x05002E00
[    5.379621] amdgpu 0000:06:00.0: amdgpu: [drm] eDP-1: PSR support 1, DC PSR ver 0, sink PSR ver 3 DPCD caps 0x3b su_y_granularity 4
[    5.379790] amdgpu 0000:06:00.0: amdgpu: [drm] DP-1: PSR support 0, DC PSR ver -1, sink PSR ver 0 DPCD caps 0x0 su_y_granularity 0
[    5.379873] amdgpu 0000:06:00.0: amdgpu: [drm] DP-2: PSR support 0, DC PSR ver -1, sink PSR ver 0 DPCD caps 0x0 su_y_granularity 0
[    5.379933] amdgpu 0000:06:00.0: amdgpu: [drm] DP-3: PSR support 0, DC PSR ver -1, sink PSR ver 0 DPCD caps 0x0 su_y_granularity 0
[    5.380312] amdgpu 0000:06:00.0: amdgpu: kiq ring mec 2 pipe 1 q 0
[    5.383501] kfd kfd: amdgpu: Allocated 3969056 bytes on gart
[    5.383516] kfd kfd: amdgpu: Total number of KFD nodes to be created: 1
[    5.384088] amdgpu: Virtual CRAT table created for GPU
[    5.384182] amdgpu: Topology: Add dGPU node [0x164e:0x1002]
[    5.384183] kfd kfd: amdgpu: added device 1002:164e
[    5.384191] amdgpu 0000:06:00.0: amdgpu: SE 1, SH per SE 1, CU per SH 2, active_cu_number 2
[    5.384194] amdgpu 0000:06:00.0: amdgpu: ring gfx_0.0.0 uses VM inv eng 0 on hub 0
[    5.384195] amdgpu 0000:06:00.0: amdgpu: ring gfx_0.1.0 uses VM inv eng 1 on hub 0
[    5.384196] amdgpu 0000:06:00.0: amdgpu: ring comp_1.0.0 uses VM inv eng 4 on hub 0
[    5.384197] amdgpu 0000:06:00.0: amdgpu: ring comp_1.1.0 uses VM inv eng 5 on hub 0
[    5.384198] amdgpu 0000:06:00.0: amdgpu: ring comp_1.2.0 uses VM inv eng 6 on hub 0
[    5.384199] amdgpu 0000:06:00.0: amdgpu: ring comp_1.3.0 uses VM inv eng 7 on hub 0
[    5.384201] amdgpu 0000:06:00.0: amdgpu: ring comp_1.0.1 uses VM inv eng 8 on hub 0
[    5.384202] amdgpu 0000:06:00.0: amdgpu: ring comp_1.1.1 uses VM inv eng 9 on hub 0
[    5.384203] amdgpu 0000:06:00.0: amdgpu: ring comp_1.2.1 uses VM inv eng 10 on hub 0
[    5.384204] amdgpu 0000:06:00.0: amdgpu: ring comp_1.3.1 uses VM inv eng 11 on hub 0
[    5.384205] amdgpu 0000:06:00.0: amdgpu: ring kiq_0.2.1.0 uses VM inv eng 12 on hub 0
[    5.384206] amdgpu 0000:06:00.0: amdgpu: ring sdma0 uses VM inv eng 13 on hub 0
[    5.384207] amdgpu 0000:06:00.0: amdgpu: ring vcn_dec_0 uses VM inv eng 0 on hub 8
[    5.384207] amdgpu 0000:06:00.0: amdgpu: ring vcn_enc_0.0 uses VM inv eng 1 on hub 8
[    5.384208] amdgpu 0000:06:00.0: amdgpu: ring vcn_enc_0.1 uses VM inv eng 4 on hub 8
[    5.384210] amdgpu 0000:06:00.0: amdgpu: ring jpeg_dec uses VM inv eng 5 on hub 8
[    5.385076] amdgpu 0000:06:00.0: amdgpu: Runtime PM not available
[    5.385247] amdgpu 0000:06:00.0: amdgpu: [drm] Skipping amdgpu DM backlight registration
[    5.385665] amdgpu 0000:06:00.0: [drm] Registered 4 planes with drm panic
[    5.385667] [drm] Initialized amdgpu 3.64.0 for 0000:06:00.0 on minor 1
[    5.390930] fbcon: amdgpudrmfb (fb0) is primary device
[    5.408489] amdgpu 0000:06:00.0: [drm] fb0: amdgpudrmfb frame buffer device
[    8.509427] amdgpu 0000:06:00.0: vgaarb: VGA decodes changed: olddecodes=io+mem,decodes=none:owns=none
[   39.576232] nvidia-nvlink: Nvlink Core is being initialized, major device number 511
[   39.579351] nvidia 0000:01:00.0: enabling device (0000 -> 0003)
[   39.579502] nvidia 0000:01:00.0: vgaarb: VGA decodes changed: olddecodes=io+mem,decodes=none:owns=none
[   39.631573] NVRM: loading NVIDIA UNIX Open Kernel Module for x86_64  590.48.01  Release Build  (root@FerneLaptop)  
[   41.064636] nvidia-modeset: Loading NVIDIA UNIX Open Kernel Mode Setting Driver for x86_64  590.48.01  Release Build  (root@FerneLaptop)  
[   41.104445] nvidia-modeset: nvidia-modeset: ACPI reported no NVIDIA native backlight available; attempting to use ACPI backlight.
[   41.107984] nvidia-modeset: WARNING: GPU:0: Unable to read EDID for display device DP-4
[   41.120638] nvidia-modeset: WARNING: GPU:0: Unable to read EDID for display device DP-4
```

粗筛一遍看下有没有 fail，发现是没有的

```bash
$ sudo dmesg | grep -i -E "(nvidia|amd)" | grep "fail"

~               ✘ 0|0|1
$ echo $?
1
```

### OpenGL 显示内容

但是在 Wayland 下会遇到如下问题

```bash
$ glxinfo | grep "OpenGL renderer string"
OpenGL renderer string: NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2
X Error of failed request:  BadDrawable (invalid Pixmap or Window parameter)
  Major opcode of failed request:  72 (X_PutImage)
  Resource id in failed request:  0x1400003
  Serial number of failed request:  52
  Current serial number in output stream:  53

$ eglinfo | grep "OpenGL renderer"
_amdgpu_device_initialize: amdgpu_query_info(ACCEL_WORKING) failed (-13)
amdgpu: amdgpu_device_initialize failed.
_amdgpu_device_initialize: amdgpu_query_info(ACCEL_WORKING) failed (-13)
amdgpu: amdgpu_device_initialize failed.
```

问 AI 得知 `glxinfo` 对 X11 环境支持比较好，对 Wayland 则一般般，因此会出现 Error 报错，而 `eglinfo` 则提示我 amdgpu 似乎没初始化成功，借助系统监视器查看发现也确实只显示了一张显卡，但是使用如下命令：

```bash
$ lspci -k | grep -E "(VGA|3D)"
01:00.0 VGA compatible controller: NVIDIA Corporation AD107M [GeForce RTX 4060 Max-Q / Mobile] (rev a1)
06:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Raphael (rev d8)
```

之后得知 NVIDIA 对应的 PCI ID 为 01:00.0， AMD 对应的则为 06:00.0

然后借助：`watch -n 1 cat /sys/bus/pci/devices/0000:01:00.0/power/runtime_status` 命令监视其运行情况，发现 AMD 卡会 active，这就很神奇了。

而在 X11 下使用的话 AMD 显卡又正常了。神奇，不过既然初始化啥的没问题那就当它没问题了，有大佬知道为什么的话欢迎交流一下。


[ArchGuide]: https://arch.icekylin.online/guide/rookie/graphic-driver
[NvidiaForums]: https://forums.developer.nvidia.com/t/understanding-nvidia-drm-modeset-1-nvidia-linux-driver-modesetting/204068/2
[ArchWiki]: https://wiki.archlinux.org/title/NVIDIA
[GeekBlogs]: https://geek-blogs.com/blog/arch-linux-nvidia
[Zido-Blog]: https://www.zido.site/blog/2021-09-04-archlinux-nvdia/

