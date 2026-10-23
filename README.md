CMP425 / CMP501 
Lab 5 TCP selector example - by Andrei Boiko
Built using SFML Sockets

### Key changes from Lab 3
- Updated Game class to handle texture loading once at initialisation.
- All textures are now passed to Tank as pointers.
- All sprite objects are now pointers for easier texture initialisation.
- Observer / Player networking in Main is rewritten using TCP.
- TankMessage now includes id value to allow differentiating between multiple tanks.
- Add Connection struct to help with handling multiple connections.

### Player Mode
Attempt to connect to the observer with default timeout (1 second). 
When connected, send positional data using blocking send calls. 

### Observer Mode
Accept multiple connections from players and handle updates using socket selector.
