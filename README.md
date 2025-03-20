# power_meter
This sample is based on the NCS SDK  v2.9.0, it's a power meter application.
It supports FTP and UART upgrade, TCP client and TCP server.
please refer to bellow test procedure to test the features.



#### FTP upgrade test

FTP upgrade test must wait for the LTE connect success,  you can check from the log "`Network registration status: Connected - home`" 。

After LTE connected, you can send below command through UART to trigger the FTP upgrade task (HEX send).

`64 6F 77 6E 6C 6F 61 64 EF BF BD EF BF BD C0`

Then wait for the upgrade to complete, you can check the uart log.

![image-20250320165718670](C:\Users\Noy\AppData\Roaming\Typora\typora-user-images\image-20250320165718670.png)



#### UART upgrade test

UART upgrade needs a PC tool to send image data to device, I've design a python tool for your test, it is `serial_upgrade.py` in the `scripts` folder.

First, open the uart terminal to check the log, when you found `I: UART init!` log, it means that the UART has initialized, you can start uart upgrade task.

Then you need to close the uart terminal, and enter the `scripts` folder to run the upgrade scripts, input below command in python environment.

`python serial_upgrade.py COM6 115200 ../binaries/patches/signed_patch.bin`



![image-20250320165521120](C:\Users\Noy\AppData\Roaming\Typora\typora-user-images\image-20250320165521120.png)

