#include <sb4/native-app.h>
#include <sb4-storage-scope.h>
#include "deck.h"

enum { PathBytes=512, NameBytes=64, BlockBytes=65536 };
enum { Invalid=0xe659u, NotFound=0x202u, Exists=0x402u };
enum { OpenItem, CopyItem, MoveItem, RenameItem, DeleteItem, CreateFolder, CreateFile, PasteItem, ClearClipboard };
enum { NewFolder, NewFile, NewName };

typedef void *(*MemorySet)(void *,int,size_t);
typedef void *(*MemoryCopy)(void *,const void *,size_t);
typedef void (*Sort)(void *,size_t,size_t,int (*)(const void *,const void *));
typedef int (*Format)(char *,size_t,const char *,...);

typedef struct {
    char path[PathBytes];
    Sb4StorageEntry *entries;
    uint32_t count, selected;
} Pane;

typedef struct {
    char stage[PathBytes], target[PathBytes];
    uint32_t source, output;
    uint64_t size, offset;
    bool stage_owned, source_open, output_open;
} Transfer;

typedef struct {
    char name[NameBytes], path[PathBytes];
    uint32_t purpose,length,cursor;
    bool caps;
} Edit;

typedef struct {
    const Sb4Compat *compat;
    const Sb4Storage *storage;
    const Sb4StorageFiles *files;
    Sb4StorageStatFunction stat;
    bool whole_sd;
    MemorySet set;
    MemoryCopy copy;
    Sort sort;
    Format format;
    Pane pane;
    char clipboard_path[PathBytes], clipboard_name[PathBytes];
    bool clipboard_move;
    uint8_t *block;
    uint32_t ack, mode, choice, error;
    char status[128], selected[PathBytes], leaf[PathBytes];
    uint32_t action_count, actions[9];
    uint64_t viewed_bytes, preview_offset, preview_size;
    uint32_t preview_lines;
    char preview[ROWS][WIDTH];
    Edit edit;
    Transfer job;
} Deck;

_Static_assert(sizeof(Deck)<=65536, "native private allocation");
static const char keys[]="abcdefghijklmnopqrstuvwxyz0123456789._- ";
static const char action_names[][32]={
    "Open", "Copy to clipboard", "Cut to clipboard", "Rename file",
    "Delete file", "New folder", "New empty file", "Paste here", "Clear clipboard",
};

static char lower(char c) { return c>='A'&&c<='Z'?(char)(c-'A'+'a'):c; }

static int compare(const char *a,const char *b) {
    while(*a&&lower(*a)==lower(*b)){a++;b++;}
    return (unsigned char)lower(*a)-(unsigned char)lower(*b);
}

static bool prefix(const char *name,const char *start) {
    while(*start)if(lower(*name++)!=*start++)return false;
    return true;
}

static bool name_allowed(const char *name) {
    if(!*name)return false;
    size_t length=0;
    for(;name[length];++length){
        unsigned char c=(unsigned char)name[length];
        if(c<32||c==127||c=='/'||c=='\\'||c==':'||c=='"'||c=='*'||c=='?'||
           c=='<'||c=='>'||c=='|')return false;
    }
    if(name[length-1]=='.'||name[length-1]==' '||prefix(name,".izdeck-copy-"))return false;
    return true;
}

static bool visible(const Deck *deck,const char *folder,const char *name) {
    if(!name_allowed(name))return false;
    bool app_root=deck->whole_sd?compare(folder,"iz2s/modules/iz-file-deck")==0:!*folder;
    return !app_root||!(compare(name,"launcher.iza")==0||compare(name,"launcher.nro")==0||
        prefix(name,"asset-")||prefix(name,"sd-bundle")||prefix(name,".launcher-")||
        prefix(name,".removed-")||prefix(name,".iz2s-version"));
}

static int order(const void *left,const void *right) {
    const Sb4StorageEntry *a=left,*b=right;
    return a->directory!=b->directory?(int)b->directory-a->directory:compare(a->name,b->name);
}

static bool join(Deck *deck,char path[PathBytes],const char *directory,const char *name) {
    if(!visible(deck,directory,name))return false;
    int n=deck->format(path,PathBytes,"%s%s%s",directory,*directory?"/":"",name);
    return n>0&&n<PathBytes;
}

static void message(Deck *deck,const char *text,uint32_t error) {
    deck->error=error;
    if(error&&error!=Exists)deck->format(deck->status,sizeof deck->status,"%s (0x%08x)",text,error);
    else deck->format(deck->status,sizeof deck->status,"%s",text);
}

static uint32_t list(IzNativeApp *app,Deck *deck,Pane *pane,const char *path,const char *select) {
    // The occupied pane remains valid when a new directory listing fails.
    Sb4StorageEntry *rows=NULL;
    uint32_t capacity=0,count=0,rc=0;
    const Sb4StorageDirectories *directories=sb4StorageDirectories(deck->storage);
    if(directories){
        uint32_t token;
        rc=directories->open(app,path,&token);
        if(rc)return rc;
        for(;;){
            if(capacity-count<SB4_STORAGE_PAGE_MAX){
                if(capacity>UINT32_MAX/2){rc=SB4_STORAGE_LIMIT;break;}
                uint32_t next=capacity?capacity*2:64;
                Sb4StorageEntry *larger=NULL;
                rc=deck->compat->allocate_aligned((void **)&larger,16,next*sizeof *rows);
                if(rc||!larger){if(!rc)rc=Invalid;break;}
                if(rows){
                    deck->copy(larger,rows,count*sizeof *rows);
                    deck->compat->release(rows);
                }
                rows=larger;
                capacity=next;
            }
            uint32_t got=0;
            rc=directories->read(app,token,rows+count,SB4_STORAGE_PAGE_MAX,&got);
            if(rc||!got)break;
            if(got>SB4_STORAGE_PAGE_MAX){rc=Invalid;break;}
            count+=got;
        }
        uint32_t closed=directories->close(app,token);
        if(!rc)rc=closed;
    }else{
        capacity=SB4_STORAGE_LIST_MAX;
        rc=deck->compat->allocate_aligned((void **)&rows,16,capacity*sizeof *rows);
        if(!rc)rc=rows?deck->storage->list(app,path,rows,capacity,&count):Invalid;
        if(!rc&&count>capacity)rc=Invalid;
    }
    if(!rc){
        uint32_t kept=0;
        for(uint32_t i=0;i<count;++i){
            if(!visible(deck,path,rows[i].name))continue;
            if(kept!=i)deck->copy(&rows[kept],&rows[i],sizeof *rows);
            ++kept;
        }
        deck->sort(rows,kept,sizeof *rows,order);
        if(pane->entries)deck->compat->release(pane->entries);
        pane->entries=rows;
        pane->count=kept;
        pane->selected=0;
        if(path!=pane->path)deck->format(pane->path,sizeof pane->path,"%s",path);
        if(select)for(uint32_t i=0;i<kept;++i)if(!compare(rows[i].name,select)){
            pane->selected=i;
            break;
        }
    }else if(rows)deck->compat->release(rows);
    return rc;
}

static void refresh(IzNativeApp *app,Deck *deck) {
    Pane *pane=&deck->pane;
    char name[SB4_STORAGE_NAME_MAX];
    const char *selected=NULL;
    if(pane->count){
        deck->format(name,sizeof name,"%s",pane->entries[pane->selected].name);
        selected=name;
    }
    uint32_t rc=list(app,deck,pane,pane->path,selected);
    if(rc)message(deck,"Could not refresh the folder",rc);
}

static uint32_t parent(IzNativeApp *app,Deck *deck,Pane *pane) {
    if(!pane->path[0])return 0;
    size_t length=0,last=0;
    while(pane->path[length]){
        if(pane->path[length]=='/')last=length+1;
        ++length;
    }
    char path[PathBytes],leaf[PathBytes];
    deck->format(leaf,sizeof leaf,"%s",pane->path+last);
    if(last)deck->copy(path,pane->path,last-1);
    path[last?last-1:0]=0;
    return list(app,deck,pane,path,leaf);
}

static uint32_t vacant(IzNativeApp *app,Deck *deck,const char *path) {
    Sb4StorageInfo info;
    uint32_t rc=deck->stat(app,path,&info);
    if(!rc)return Exists;
    return (rc&0x3fffffu)==NotFound?0:rc;
}

static uint32_t close_transfer(IzNativeApp *app,Deck *deck,uint32_t error,bool cancel) {
    Transfer *job=&deck->job;
    uint32_t rc;
    if(job->output_open){
        rc=deck->files->close(app,job->output);
        if(!error)error=rc;
    }
    if(job->source_open){
        rc=deck->files->close(app,job->source);
        if(!error)error=rc;
    }
    if(job->stage_owned&&(cancel||error)){
        rc=deck->files->remove_file(app,job->stage);
        if(!rc)rc=deck->files->commit(app);
        if(!error)error=rc;
    }
    deck->set(job,0,sizeof *job);
    return error;
}

static uint32_t start_copy(IzNativeApp *app,Deck *deck,const char *target) {
    Transfer *job=&deck->job;
    if(!compare(deck->clipboard_path,target))return Exists;
    uint32_t rc=vacant(app,deck,target);
    if(rc)return rc;
    const char *destination=deck->pane.path;
    int n=deck->format(job->stage,sizeof job->stage,"%s%s.izdeck-copy-%llx.tmp",destination,
                       *destination?"/":"",(unsigned long long)app->frames);
    if(n<0||n>=PathBytes)return Invalid;
    deck->format(job->target,sizeof job->target,"%s",target);
    rc=deck->files->open(app,deck->clipboard_path,SB4_STORAGE_OPEN_READ,&job->source,&job->size);
    if(rc)return close_transfer(app,deck,rc,true);
    job->source_open=true;
    rc=deck->files->create(app,job->stage,job->size);
    if(rc)return close_transfer(app,deck,rc,true);
    job->stage_owned=true;
    uint64_t ignored;
    rc=deck->files->open(app,job->stage,SB4_STORAGE_OPEN_UPDATE,&job->output,&ignored);
    if(rc)return close_transfer(app,deck,rc,true);
    job->output_open=true;
    deck->mode=Copying;
    message(deck,"Copying into this folder",0);
    return 0;
}

static void step_copy(IzNativeApp *app,Deck *deck) {
    Transfer *job=&deck->job;
    uint32_t rc=0;
    uint64_t remaining=job->size-job->offset;
    if(remaining){
        uint32_t bytes=remaining<BlockBytes?(uint32_t)remaining:BlockBytes,got=0;
        rc=deck->files->read(app,job->source,job->offset,deck->block,bytes,&got);
        if(!rc&&got!=bytes)rc=Invalid;
        if(!rc)rc=deck->files->write(app,job->output,job->offset,deck->block,bytes);
        if(!rc)job->offset+=bytes;
    }
    if(!rc&&job->offset<job->size)return;
    uint64_t source_size=0;
    if(!rc)rc=deck->files->size(app,job->source,&source_size);
    if(!rc&&source_size!=job->size)rc=Invalid;
    if(!rc)rc=deck->files->flush(app,job->output);
    if(job->output_open){
        uint32_t closed=deck->files->close(app,job->output);
        job->output_open=false;
        if(!rc)rc=closed;
    }
    if(!rc)rc=vacant(app,deck,job->target);
    if(!rc)rc=deck->files->rename_file(app,job->stage,job->target);
    if(!rc){
        job->stage_owned=false;
        rc=deck->files->commit(app);
    }
    rc=close_transfer(app,deck,rc,rc!=0);
    deck->mode=Browse;
    message(deck,rc?"Copy could not be completed":"Copy complete",rc);
    refresh(app,deck);
}

static unsigned char cyrillic(unsigned char first,unsigned char second){
    if((first!=0xd0&&first!=0xd1)||(second&0xc0)!=0x80)return 0;
    unsigned character=(first&0x1fu)*64u+(second&63u);
    return character>=0x400&&character<=0x45f?(unsigned char)(128u+character-0x400u):0;
}

static uint32_t preview(IzNativeApp *app,Deck *deck,uint64_t offset) {
    uint32_t handle,got=0;
    uint64_t size;
    uint32_t rc=deck->files->open(app,deck->selected,SB4_STORAGE_OPEN_READ,&handle,&size);
    if(rc)return rc;
    if(offset>=size)offset=0;
    uint64_t remaining=size-offset;
    // A 144-byte page fits every binary byte into 12 hex rows.
    const uint32_t bytes=remaining<144?(uint32_t)remaining:144;
    if(bytes)rc=deck->files->read(app,handle,offset,deck->block,bytes,&got);
    uint32_t closed=deck->files->close(app,handle);
    if(rc||closed||got!=bytes)return rc?rc:closed?closed:Invalid;
    bool hex=false;
    for(uint32_t i=0;i<bytes;++i){
        uint8_t ch=deck->block[i];
        if(ch==0||(ch<32&&ch!='\r'&&ch!='\n'&&ch!='\t')){hex=true;break;}
    }
    deck->set(deck->preview,0,sizeof deck->preview);
    deck->preview_offset=offset;
    deck->preview_size=size;
    deck->viewed_bytes=bytes;
    deck->preview_lines=0;
    if(hex){
        for(uint32_t i=0;i<bytes;i+=12){
            char *line=deck->preview[deck->preview_lines++];
            int n=deck->format(line,WIDTH,"%08llx  ",(unsigned long long)(offset+i));
            for(uint32_t j=i;j<i+12&&j<bytes;++j)
                n+=deck->format(line+n,WIDTH-(size_t)n,"%02x ",deck->block[j]);
        }
    }else if(bytes){
        unsigned row=0,column=0;
        uint32_t i=0;
        for(;i<bytes&&row<ROWS;i++){
            unsigned char ch=deck->block[i];
            if(ch=='\r')continue;
            if(ch=='\n'||column>=80){
                ++row;
                column=0;
                if(ch=='\n')continue;
                if(row>=ROWS)break;
            }
            if((ch&0xc0)==0x80)continue;
            unsigned char letter=i+1<bytes?cyrillic(ch,deck->block[i+1]):0;
            if(letter)++i;
            deck->preview[row][column++]=letter?(char)letter:(ch>=32&&ch<127)?(char)ch:ch=='\t'?' ':'?';
        }
        deck->viewed_bytes=i;
        deck->preview_lines=row<ROWS?row+1:ROWS;
    }
    if(!bytes)deck->format(deck->preview[deck->preview_lines++],WIDTH,"Empty file");
    deck->mode=Preview;
    return 0;
}

static void begin_edit(Deck *deck,uint32_t purpose) {
    Edit *edit=&deck->edit;
    deck->set(edit,0,sizeof *edit);
    edit->purpose=purpose;
    if(purpose==NewName){
        deck->format(edit->path,sizeof edit->path,"%s",deck->selected);
        size_t length=0;
        bool ascii=true;
        for(;deck->leaf[length];++length)if((unsigned char)deck->leaf[length]>126)ascii=false;
        if(ascii&&length<NameBytes){
            deck->format(edit->name,sizeof edit->name,"%s",deck->leaf);
            edit->length=(uint32_t)length;
        }
    }
    deck->mode=Keyboard;
    message(deck,"",0);
}

static void edit_input(IzNativeApp *app,Deck *deck,uint32_t keys_down) {
    Edit *edit=&deck->edit;
    if(keys_down&B){deck->mode=Browse;return;}
    if(keys_down&Y)edit->caps=!edit->caps;
    if(keys_down&Left)edit->cursor=(edit->cursor+39)%40;
    if(keys_down&Right)edit->cursor=(edit->cursor+1)%40;
    if(keys_down&Up)edit->cursor=(edit->cursor+30)%40;
    if(keys_down&Down)edit->cursor=(edit->cursor+10)%40;
    if((keys_down&X)&&edit->length)edit->name[--edit->length]=0;
    if((keys_down&A)&&edit->length+1<NameBytes){
        char ch=keys[edit->cursor];
        edit->name[edit->length++]=edit->caps&&ch>='a'&&ch<='z'?(char)(ch-'a'+'A'):ch;
        edit->name[edit->length]=0;
    }
    if(!(keys_down&Plus))return;
    char target[PathBytes];
    Pane *pane=&deck->pane;
    uint32_t rc=join(deck,target,pane->path,edit->name)?vacant(app,deck,target):Invalid;
    if(!rc){
        if(edit->purpose==NewFolder)rc=deck->files->mkdir(app,target);
        else if(edit->purpose==NewFile)rc=deck->files->create(app,target,0);
        else rc=deck->files->rename_file(app,edit->path,target);
        if(!rc)rc=deck->files->commit(app);
    }
    if(rc){
        message(deck,rc==Exists?"A file or folder already uses this name":"Could not save this name",rc);
        return;
    }
    deck->mode=Browse;
    message(deck,edit->purpose==NewName?"File renamed":"Created on SD",0);
    refresh(app,deck);
}

static uint32_t open_item(IzNativeApp *app,Deck *deck) {
    Pane *pane=&deck->pane;
    if(!pane->count)return 0;
    const Sb4StorageEntry *item=&pane->entries[pane->selected];
    if(!join(deck,deck->selected,pane->path,item->name))return Invalid;
    deck->format(deck->leaf,sizeof deck->leaf,"%s",item->name);
    if(!item->directory)return preview(app,deck,0);
    return list(app,deck,pane,deck->selected,NULL);
}

static void action_menu(Deck *deck) {
    Pane *pane=&deck->pane;
    deck->choice=0;
    deck->action_count=0;
    if(pane->count){
        const Sb4StorageEntry *item=&pane->entries[pane->selected];
        if(!join(deck,deck->selected,pane->path,item->name)){
            message(deck,"The selected path is too long",Invalid);
            return;
        }
        deck->format(deck->leaf,sizeof deck->leaf,"%s",item->name);
        deck->actions[deck->action_count++]=OpenItem;
        if(!item->directory){
            deck->actions[deck->action_count++]=CopyItem;
            deck->actions[deck->action_count++]=MoveItem;
            deck->actions[deck->action_count++]=RenameItem;
            deck->actions[deck->action_count++]=DeleteItem;
        }
    }
    deck->actions[deck->action_count++]=CreateFolder;
    deck->actions[deck->action_count++]=CreateFile;
    if(deck->clipboard_path[0]){
        deck->actions[deck->action_count++]=PasteItem;
        deck->actions[deck->action_count++]=ClearClipboard;
    }
    deck->mode=Actions;
    message(deck,"",0);
}

static void paste(IzNativeApp *app,Deck *deck){
    char target[PathBytes];
    uint32_t rc=0;
    if(!join(deck,target,deck->pane.path,deck->clipboard_name))rc=Invalid;
    else if(!deck->clipboard_move)rc=start_copy(app,deck,target);
    else{
        rc=vacant(app,deck,target);
        if(!rc)rc=deck->files->rename_file(app,deck->clipboard_path,target);
        if(!rc){
            deck->clipboard_path[0]=0;
            rc=deck->files->commit(app);
        }
        if(!rc){message(deck,"File moved",0);refresh(app,deck);}
    }
    if(rc)message(deck,rc==Exists?"This folder already contains a file with that name":"Could not paste here",rc);
}

static void selected_action(IzNativeApp *app,Deck *deck) {
    const uint32_t action=deck->actions[deck->choice];
    uint32_t rc=0;
    deck->mode=Browse;
    if(action==CreateFolder||action==CreateFile)begin_edit(deck,action==CreateFolder?NewFolder:NewFile);
    else if(action==OpenItem)rc=open_item(app,deck);
    else if(action==RenameItem)begin_edit(deck,NewName);
    else if(action==DeleteItem)deck->mode=Confirm;
    else if(action==PasteItem)paste(app,deck);
    else if(action==ClearClipboard){
        deck->clipboard_path[0]=0;
        message(deck,"Clipboard cleared",0);
    }else{
        deck->format(deck->clipboard_path,sizeof deck->clipboard_path,"%s",deck->selected);
        deck->format(deck->clipboard_name,sizeof deck->clipboard_name,"%s",deck->leaf);
        deck->clipboard_move=action==MoveItem;
        message(deck,deck->clipboard_move?"Cut: open the destination and press Y":"Copied: open the destination and press Y",0);
    }
    if(rc)message(deck,"Action could not be completed",rc);
}

static void input(IzNativeApp *app,Deck *deck,uint32_t pressed,uint32_t touch) {
    if(deck->mode==Copying){
        if(pressed&B){
            uint32_t rc=close_transfer(app,deck,0,true);
            deck->mode=Browse;
            message(deck,rc?"Could not clean up the incomplete copy":"Copy cancelled",rc);
        }
        return;
    }
    if(deck->mode==Keyboard){edit_input(app,deck,pressed);return;}
    if(deck->mode==Confirm){
        if(pressed&B)deck->mode=Browse;
        else if(pressed&A){
            uint32_t rc=deck->files->remove_file(app,deck->selected);
            if(!rc)rc=deck->files->commit(app);
            deck->mode=Browse;
            message(deck,rc?"Could not delete the selected file":"File deleted",rc);
            refresh(app,deck);
        }
        return;
    }
    if(deck->mode==Preview){
        uint32_t rc=0;
        if(pressed&B)deck->mode=Browse;
        else if((pressed&R)&&deck->viewed_bytes&&deck->preview_offset+deck->viewed_bytes<deck->preview_size)
            rc=preview(app,deck,deck->preview_offset+deck->viewed_bytes);
        else if((pressed&L)&&deck->preview_offset)
            rc=preview(app,deck,deck->preview_offset>=144?deck->preview_offset-144:0);
        if(rc)message(deck,"Could not read file preview",rc);
        return;
    }
    if(deck->mode==Actions){
        if(pressed&B)deck->mode=Browse;
        else if(pressed&A)selected_action(app,deck);
        else if(pressed&Up)deck->choice=(deck->choice+deck->action_count-1)%deck->action_count;
        else if(pressed&Down)deck->choice=(deck->choice+1)%deck->action_count;
        return;
    }
    Pane *pane=&deck->pane;
    if(pane->count){
        if((pressed&TouchRow)&&(touch&255u)<ROWS){
            uint32_t row=pane->selected/ROWS*ROWS+(touch&255u);
            if(row<pane->count)pane->selected=row;
        }
        if((pressed&Up)&&pane->selected)pane->selected--;
        if((pressed&Down)&&pane->selected+1<pane->count)pane->selected++;
        if(pressed&L)pane->selected=pane->selected>=ROWS?pane->selected-ROWS:0;
        if(pressed&R)pane->selected=pane->selected+ROWS<pane->count?pane->selected+ROWS:pane->count-1;
    }
    uint32_t rc=0;
    if(pressed&X)action_menu(deck);
    else if(pressed&B)rc=parent(app,deck,pane);
    else if(pressed&A)rc=open_item(app,deck);
    else if((pressed&Y)&&deck->clipboard_path[0])paste(app,deck);
    else if(pressed&Y){message(deck,"Refreshing folders",0);refresh(app,deck);if(!deck->error)message(deck,"Folders refreshed",0);}
    if(rc)message(deck,"Could not open the selected item",rc);
}

static void display(char *out,size_t limit,const char *in) {
    size_t index=0;
    for(;*in&&index+1<limit;++in){
        const unsigned char byte=(unsigned char)*in;
        if((byte&0xc0)==0x80)continue;
        unsigned char letter=cyrillic(byte,(unsigned char)in[1]);
        if(letter)++in;
        out[index++]=letter?(char)letter:byte>=32&&byte<=126?(char)byte:'?';
    }
    if(*in&&index>=3)out[index-3]=out[index-2]=out[index-1]='.';
    out[index]=0;
}

static void publish(Deck *deck,Mailbox *view) {
    deck->set((uint8_t *)view+64,0,sizeof(*view)-64);
    view->magic=DECK_MAGIC;
    view->ack=deck->ack;
    view->mode=deck->mode;
    view->clipboard=deck->clipboard_path[0]?1u:0u;
    view->error=deck->error;
    view->whole_sd=deck->whole_sd;
    display(view->status,sizeof view->status,deck->status);
    {
        const Pane *pane=&deck->pane;
        PaneView *panel=&view->pane;
        deck->format(panel->path,sizeof panel->path,"%s",deck->whole_sd?"SD:/":"SD:/iz2s/modules/iz-file-deck/");
        size_t at=deck->whole_sd?4u:sizeof("SD:/iz2s/modules/iz-file-deck/")-1u;
        display(panel->path+at,sizeof panel->path-at,pane->path);
        panel->count=pane->count;
        panel->selected=pane->selected;
        panel->first=pane->selected/ROWS*ROWS;
        uint32_t remaining=pane->count-panel->first;
        panel->shown=remaining<ROWS?remaining:ROWS;
        for(unsigned i=0;i<panel->shown;++i){
            const Sb4StorageEntry *entry=&pane->entries[panel->first+i];
            Row *row=&panel->rows[i];
            row->folder=entry->directory;
            display(row->name,sizeof row->name,entry->name);
            if(entry->directory)deck->format(row->size,sizeof row->size,"Folder");
            else if(entry->size>=1048576)deck->format(row->size,sizeof row->size,"%llu MiB",(unsigned long long)(entry->size/1048576));
            else if(entry->size>=1024)deck->format(row->size,sizeof row->size,"%llu KiB",(unsigned long long)(entry->size/1024));
            else deck->format(row->size,sizeof row->size,"%llu B",(unsigned long long)entry->size);
        }
    }
    if(deck->mode==Actions){
        deck->format(view->title,sizeof view->title,"Actions");
        view->count=deck->action_count;
        view->index=deck->choice;
        for(unsigned i=0;i<view->count;++i){
            unsigned action=deck->actions[i];
            display(view->lines[i],WIDTH,action_names[action]);
        }
    }else if(deck->mode==Confirm){
        deck->format(view->title,sizeof view->title,"Delete this file?");
        display(view->lines[0],WIDTH,deck->leaf);
        view->count=1;
    }else if(deck->mode==Preview){
        display(view->title,sizeof view->title,deck->leaf);
        view->count=deck->preview_lines;
        deck->copy(view->lines,deck->preview,sizeof deck->preview);
        deck->format(view->input,sizeof view->input,"Offset %llu / %llu bytes",
                   (unsigned long long)deck->preview_offset,(unsigned long long)deck->preview_size);
    }else if(deck->mode==Keyboard){
        view->index=deck->edit.cursor;
        view->caps=deck->edit.caps;
        deck->format(view->title,sizeof view->title,"%s",
                     deck->edit.purpose==NewName?"Rename file":deck->edit.purpose==NewFolder?"New folder":"New empty file");
        display(view->input,sizeof view->input,deck->edit.name);
    }else if(deck->mode==Copying){
        deck->format(view->title,sizeof view->title,"Copying file");
        display(view->lines[0],WIDTH,deck->clipboard_name);
        view->count=1;
        Transfer *job=&deck->job;
        uint64_t unit=job->size/1000;
        view->progress=job->size?(uint32_t)(unit?job->offset/unit:job->offset*1000/job->size):0;
        if(view->progress>1000)view->progress=1000;
    }
}

__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp *app,uint32_t event) {
    const Sb4Compat *compat=sb4ModuleCompat(app);
    if(!compat||!app->data||app->data_bytes<sizeof(Deck)||!app->controls||
       app->controls_bytes<MAILBOX+sizeof(Mailbox))return Invalid;
    Deck *deck=app->data;
    Mailbox *view=(void *)((uint8_t *)app->controls+MAILBOX);
    if(event==IZ_NATIVE_STOP){
        if(deck->files)close_transfer(app,deck,0,true);
        if(deck->pane.entries){
            compat->release(deck->pane.entries);
            deck->pane.entries=NULL;
        }
        if(deck->block){compat->release(deck->block);deck->block=NULL;}
        return 0;
    }
    if(event==IZ_NATIVE_INIT){
        deck->compat=compat;
        deck->storage=sb4ModuleStorage(app);
        deck->whole_sd=iz2sStorageHasWholeSd(deck->storage);
        deck->files=sb4StorageFileChanges(deck->storage);
        deck->stat=sb4StorageStat(deck->storage);
        deck->set=(MemorySet)compat->resolve_c("memset");
        deck->copy=(MemoryCopy)compat->resolve_c("memcpy");
        deck->sort=(Sort)compat->resolve_c("qsort");
        deck->format=(Format)compat->resolve_c("snprintf");
        if(!deck->files||!deck->stat||!deck->set||!deck->copy||!deck->sort||!deck->format)return Invalid;
        uint32_t rc=compat->allocate_aligned((void **)&deck->block,16,BlockBytes);
        if(rc||!deck->block)return rc?rc:Invalid;
        refresh(app,deck);
        publish(deck,view);
        return 0;
    }
    uint32_t sequence=view->seq;
    bool action=sequence!=deck->ack,copied=deck->mode==Copying;
    if(action){
        deck->ack=sequence;
        input(app,deck,view->keys,view->touch);
    }
    if(deck->mode==Copying)step_copy(app,deck);
    if(action||copied)publish(deck,view);
    return 0;
}
