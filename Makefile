# Compiler
CC = gcc

# Compiler flags: 
# -g for debugging
# -Wall for all warnings
# -Iprotocol tells gcc to look in the "protocol" folder for headers
CFLAGS = -g -Wall -Iprotocol

# --- Target Executables ---
# We now define the *full path* for the final executables
CLIENT_EXEC = client_app
NS_EXEC = name_server
SS_EXEC = storage_server

# --- Object Files ---
# The .o files also go into their respective folders
CLIENT_OBJ = client/client.o client/handlers.o
NS_OBJ = name/name_server.o name/hash_cache.o name/fault_tolerance.o name/catalog.o
SS_OBJ = storage/storage_server.o storage/locks.o storage/document.o storage/metadata.o

# --- Header Files ---
COMMON_HEADER = protocol/protocol.h

# --- Build Rules ---

# Default rule: build all three executables
all: $(CLIENT_EXEC) $(NS_EXEC) $(SS_EXEC)

# --- 1. Linking Rules ---
# Creates final executables from .o files
# $@ is the target (e.g., "client/client_app")
# The output file ($@) is now placed inside the folder
$(CLIENT_EXEC): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJ)

$(NS_EXEC): $(NS_OBJ)
	$(CC) $(CFLAGS) -o $@ $(NS_OBJ)

$(SS_EXEC): $(SS_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SS_OBJ)

# --- 2. Compilation Rules ---
# Creates .o files from .c files
# $@ is the target (e.g., "client/client.o")
# $< is the source (e.g., "client/client.c")
# The output file ($@) is placed inside the folder
client/client.o: client/client.c $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

client/handlers.o: client/handlers.c client/handlers.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

name/name_server.o: name/name_server.c $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

name/hash_cache.o: name/hash_cache.c name/hash_cache.h name/name_globals.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

name/fault_tolerance.o: name/fault_tolerance.c name/fault_tolerance.h name/name_globals.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

name/catalog.o: name/catalog.c name/catalog.h name/name_globals.h name/hash_cache.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

storage/storage_server.o: storage/storage_server.c $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

storage/locks.o: storage/locks.c storage/locks.h storage/storage_globals.h storage/document.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

storage/document.o: storage/document.c storage/document.h storage/storage_globals.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

storage/metadata.o: storage/metadata.c storage/metadata.h storage/storage_globals.h storage/document.h $(COMMON_HEADER)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Cleanup Rule ---
clean:
	# rm -f will correctly remove the files from within their folders
	rm -f $(CLIENT_EXEC) $(NS_EXEC) $(SS_EXEC)
	rm -f $(CLIENT_OBJ) $(NS_OBJ) $(SS_OBJ)