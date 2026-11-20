-- @name Hello
print("Hello from the SD card!")
print("Battery:", battery())
print("Wi-Fi:", wifi())
sleep(1000)
cmd("ls /")
wait_key()
print("Bye")
