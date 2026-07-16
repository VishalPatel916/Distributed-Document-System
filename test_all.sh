#!/bin/bash

# Cleanup
rm -rf ss_storage
killall name_server storage_server client_app 2>/dev/null
sleep 1

# Start servers
./name_server > ns_test.log 2>&1 &
sleep 1
./storage_server 8082 ./ss_storage 127.0.0.1 127.0.0.1 > ss_test.log 2>&1 &
sleep 1

echo "--- Test 1: CREATE and WRITE ---"
cat << 'CLIENT_CMD' > client_script.txt
testuser
CREATE doc1.txt
WRITE doc1.txt 0 Hello world from test script!
READ doc1.txt
exit
CLIENT_CMD

./client_app 127.0.0.1 < client_script.txt > client1.log 2>&1

echo "--- Test 2: ADD ACCESS and READ as another user ---"
cat << 'CLIENT_CMD' > client_script2.txt
testuser
ADD_ACCESS doc1.txt guestuser READ
exit
CLIENT_CMD
./client_app 127.0.0.1 < client_script2.txt > client2.log 2>&1

cat << 'CLIENT_CMD' > client_script3.txt
guestuser
READ doc1.txt
exit
CLIENT_CMD
./client_app 127.0.0.1 < client_script3.txt > client3.log 2>&1

echo "--- Test 3: DELETE ---"
cat << 'CLIENT_CMD' > client_script4.txt
testuser
DELETE doc1.txt
exit
CLIENT_CMD
./client_app 127.0.0.1 < client_script4.txt > client4.log 2>&1

# Stop servers
killall name_server storage_server
echo "Done"
