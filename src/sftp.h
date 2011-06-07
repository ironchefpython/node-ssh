#ifndef NODE_SFTP_SFTP_H
#define NODE_SFTP_SFTP_H

#include <node.h>
#include <node_object_wrap.h>
#include <node_events.h>
#include <v8.h>

#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <libssh/callbacks.h>

//#include <cerrno>

using namespace v8;
using namespace node;

struct ListNode {  
  ListNode();
  ~ListNode();
  char* data;
  sftp_attributes attr;
  ListNode* next;
  void add(const char* data, sftp_attributes attr=NULL );
};

class SFTP : EventEmitter {
private:
    ssh_session m_ssh_session;
    sftp_session m_sftp_session;
    sftp_file m_sftp_file;
    ssh_string m_pub_key;
    ssh_private_key m_prv_key;    
    long m_timeout;
    int m_done;
    char* m_error;
    ListNode* m_list;
    sftp_attributes m_stat;
    char* m_wdata;
    char* m_path;
    char* m_path2;
    size_t m_size;
    size_t m_pos;
    int m_int;
    int m_int2;
  
    static Persistent<FunctionTemplate> constructor_template;
    static Persistent<ObjectTemplate> data_template;
    static Persistent<String> callback_symbol;
    
    static int startConnect(eio_req *req);
    static int startMkdir(eio_req *req);
    static int startWriteFile(eio_req *req);
    static int continueWriteFile(eio_req *req);
    static int cbWriteFile(eio_req *req);
    static int startReadFile(eio_req *req);
    static int continueReadFile(eio_req *req);
    static int cbReadFile(eio_req *req);
    static int startListDir(eio_req *req);
    static int startRename(eio_req *req);
    static int startChmod(eio_req *req);
    static int startChown(eio_req *req);
    static int startStat(eio_req *req);
    static int startUnlink(eio_req *req);
    static int startRmdir(eio_req *req);
    static int startExec(eio_req *req);
    static int startSetPubKey(eio_req *req);
    static int startSetPrvKey(eio_req *req);    
    static int onDone(eio_req *req);
    static int onStat(eio_req *req);
    static int onList(eio_req *req);

    void freeSessions();
    void setCharData(char*& to, Local<Value> data);
    void resetData();
protected:
    static Handle<Value> New(const Arguments &args);

public:
    SFTP(const Arguments &);
    virtual ~SFTP();
    
    static void Initialize(Handle<Object> &);
    
    static Handle<Value> init(const Arguments &args);
    static Handle<Value> connect(const Arguments &args);
    static Handle<Value> mkdir(const Arguments &args);
    static Handle<Value> writeFile(const Arguments &args);
    static Handle<Value> readFile(const Arguments &args);
    static Handle<Value> listDir(const Arguments &args);
    static Handle<Value> rename(const Arguments &args);
    static Handle<Value> chmod(const Arguments &args);
    static Handle<Value> chown(const Arguments &args);
    static Handle<Value> stat(const Arguments &args);
    static Handle<Value> unlink(const Arguments &args);
    static Handle<Value> rmdir(const Arguments &args);
    static Handle<Value> exec(const Arguments &args);
    static Handle<Value> setPubKey(const Arguments &args);
    static Handle<Value> setPrvKey(const Arguments &args);
    static Handle<Value> isConnected(const Arguments &args);
    static Handle<Value> interrupt(const Arguments &args);
};

#endif