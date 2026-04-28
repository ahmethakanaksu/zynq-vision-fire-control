# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\mfurk\workspace_zybo_clean\zybo_platform\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\mfurk\workspace_zybo_clean\zybo_platform\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {zybo_platform}\
-hw {C:\Users\mfurk\zybo_project_2\zybo_base_uart_eth.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -fsbl-target {psu_cortexa53_0} -out {C:/Users/mfurk/workspace_zybo_clean}

platform write
platform generate -domains 
platform active {zybo_platform}
platform generate
bsp reload
bsp write
platform generate -domains 
bsp setlib -name lwip211 -ver 1.3
bsp removelib -name lwip211
bsp setlib -name lwip211 -ver 1.3
bsp write
bsp reload
catch {bsp regenerate}
platform generate -domains standalone_domain 
platform active {zybo_platform}
domain active {zynq_fsbl}
bsp reload
domain active {standalone_domain}
bsp reload
bsp config phy_link_speed "CONFIG_LINKSPEED100"
bsp write
bsp reload
catch {bsp regenerate}
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform active {zybo_platform}
platform generate -domains 
platform active {zybo_platform}
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform active {zybo_platform}
platform clean
platform generate
