import socket
import time

host = '117.185.16.31'
port = 80
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
client.connect((host, port))

while True:
    client.send('''GET / HTTP/1.1\r\n
        Host: www.baidu.com\r\n
        User-Agent: curl/7.54.0\r\n
        Accept: */*\r\n\r\n'''.encode())
    print('''GET / HTTP/1.1\r\n
            Host: www.baidu.com\r\n
            User-Agent: curl/7.54.0\r\n
            Accept: */*\r\n\r\n''')
    data = client.recv(1024)
    print(data)
    time.sleep(5)