/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */

#ifndef NODE_SSH_SSH_H
#define NODE_SSH_SSH_H

#include <node.h>
#include <node_object_wrap.h>
#include <node_events.h>
#include <v8.h>

#include <libssh/libssh.h>

using namespace v8;
using namespace node;

#define SSH_READ_BUFFER_SIZE 4096
#define SSH_MAX_ERROR 2048
#define SSH_MIN(a,b) ((a) < (b) ? (a) : (b))

class SSHBase : public EventEmitter {
protected:
    ssh_session m_ssh_session;
    ssh_string m_pub_key;
    ssh_private_key m_prv_key;
    int m_done;
    char* m_error;
    char* m_wdata;
    
    static Persistent<String> callback_symbol;
    static Persistent<String> stdout_symbol;
    static Persistent<String> stderr_symbol;    
    
    static int startSetPubKey(eio_req *req);
    static int startSetPrvKey(eio_req *req); 
    static int startConnect(eio_req *req);
    static int onDone(eio_req *req);

    virtual void freeSessions();
    virtual void resetData();
public:
    SSHBase(const Arguments &);
    virtual ~SSHBase();
    
    static void Initialize(Handle<Object> &);

    static Handle<Value> init(const Arguments &args);
    static Handle<Value> connect(const Arguments &args);
    static Handle<Value> interrupt(const Arguments &args);
    static Handle<Value> setPubKey(const Arguments &args);
    static Handle<Value> setPrvKey(const Arguments &args);    
};

void setOption(ssh_session& session, Local<Object>& obj, const char* prop_name, 
                ssh_options_e opt); 
void setMember(char*& member, Local<Object>& obj, const char* prop_name);
void setMember(int& member, Local<Object>& obj, const char* prop_name);
void setCharData(char*& to, Local<Value> data);
Handle<Value> createBuffer(char* data, size_t size);

#endif