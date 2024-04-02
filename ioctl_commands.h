#ifndef IOCTL_COMMANDS
#define IOCTL_COMMANDS

//Defined command codes for ioctl operations
#define GET_BUFFER_SIZE 0   //Command to get current size of the buffer in bytes
#define SET_BUFFER_SIZE 1  //Command to set a new size for the buffer in bytes
#define GET_MAX_NR_PROCESSES 2  //Command to get maximum number of processes allowed to acess the device
#define SET_MAX_NR_PROCESSES 3  //Command to set maximum number of processes allowed to acess the device
#define GET_BUFFER_FREE_SPACE 4  //Command to query the amount of free space in the device buffer
#define GET_BUFFER_USED_SPACE 5  //Command to query the aomunt of used space in the device buffer

//Defined constants for our device managment 
#define DEVICE_COUNT 2  //The number of devices manged by the driver
#define BUFFER_COUNT 2  //The Number of buffers associated with each device

#endif /* end of include guard: IOCTL_COMMANDS */
