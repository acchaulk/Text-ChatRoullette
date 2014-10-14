CS 513 Project 1 - Text ChatRoullette
Adam Chaulk and Yang Lu

Compile:
To compile the code, navigate to the root directory of the project and type "make all." You can also run "make clean" to remove 
the binaries and object files.

Running the Server:
To run the server, run the executable by typing "./server". This will open the administrator shell. To start the server, allowing
other users to connect, you must type "/start". While running, the admin has the option to enter the following commands:
	"/stats" - creates a log file under the "log" directory containing information about the TRS server
	"/throwout <user>" - kicks out the user from the current chat session
	"/block <user>" - user cannot start another chat
	"/unblock <user>" - unblocks the user from chatting
	"/end" - destroys chat channels and informs clients that their session has ended.
	
Running the client:
To run the client program, run the executable by typing "./client". This opens the shell for the user to type in. To connect to a server,
you must type "/connect <hostname>". Once connected to a server, the user can input the following commands:
	"/chat" - informs the TRS that the user wishes to be paired with another user to chat
	"/quit" - quits the current chat channel and puts them back in the queue
	"/transfer <path/to/file>" - transfers the specified file to the chat partner if the size is under 100 MB
	"/flag" - flags the user, in forming the TRS that the partner is misbehaving
	"/help" - lists commands the client can enter