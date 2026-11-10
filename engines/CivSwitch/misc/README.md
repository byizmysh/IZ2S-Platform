This is an old method I used to send the exploit without Pico.  
It should also work on the JP Switch 2.  

**Requirements:**

- A modded Switch 1  
- Two copies of the Civ 7 game cartridge  

**Setup:**

The full modified file is not shipped here (it contains the game's original code) - only the changes: `screen-mp-chat.patch`. Apply it to the pristine file from your own game dump:

```
patch -p1 < screen-mp-chat.patch   # in the romfs dump root, patches Base/modules/core/ui/mp-chat/screen-mp-chat.js
```

Then, just like with a romfs mod, replace `screen-mp-chat.js` in:  

`romfs:/Base/modules/core/ui/mp-chat`  

**Steps:**

1. Create a local multiplayer host room on the Switch 2 and open the chat box.  
2. Using the modded Switch 1, join the room and open the chat box.  
3. On the Switch 1, open the chat's on-screen keyboard, enter any character, and press OK to send it.  

The Remote JS Loader payload will then be sent to the Switch 2 through the network chat.  

Technically, you don't need a modded Switch 1 if you have Pico.  
You can simply use Pico to type and send the payload from one Switch to another through network chat.  