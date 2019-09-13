# coding : utf-8

import socket
byte = 1024
port = 4312
host = ""
addr = (host, port)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(addr)
print("waiting to receive messages...")

while True:
    (data, addr) = sock.recvfrom(byte)
    text = data.decode('utf-8')
    if text == 'exit':
        break
    else :
        print('The client at {} says {!r}'.format(addr, text))
        text = 'Your data was {}bytes long'.format(len(data))
        data = text.encode('utf-8')
        sock.sendto(data, addr)

sock.close()