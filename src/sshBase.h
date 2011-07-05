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
#include <stdlib.h>

using namespace v8;
using namespace node;

#define SSH_READ_BUFFER_SIZE 4096
#define SSH_MAX_ERROR 2048
#define SSH_MIN(a,b) ((a) < (b) ? (a) : (b))

/**
Since libssh has no full async api, for each operation there is a task created
that can be handled by a worker thread, this way we can act asynchronous.
So for example the exported connect function create an eio_custom with the
startConnect and onDone functionpointers then returns. After that a worker
thread will handle the startConnect function and when it's done, it calls
onDone from the main thread, from here we emitt an event to notice the 
javascript layer about the result. Keep in mind that v8 cannot be touched from a 
worker thread, so all the data that is needed for a task must be stored on 
the object.
Thread safety. Libssh is not entirely thread safe (more details in their
documentation) so the javascript layer of this lib is making sure that no new
task is being called while the previoud one is not done. This makes also sure
that the data members on the object are not accessed from multiple threads in 
the same time. 
**/

class SSHBase : public EventEmitter {
protected:
    ssh_session m_ssh_session;
    ssh_string m_pub_key;
    ssh_private_key m_prv_key;
    // used for longer asynch operations (file read/write, spawn), where 
    // instead of blocking a thread till the operation is getting completed
    // we break the operation into several rounds, when there is no more round
    // this flag is 1
    int m_done;
    // used for reporting back errors
    char* m_error;
    // buffer for data to write
    char* m_wdata;
    
    // name of events to emitt are stored in these static v8 strings
    static Persistent<String> callback_symbol;
    static Persistent<String> stdout_symbol;
    static Persistent<String> stderr_symbol;    
    
    // blocking operations tasks for the worker threads of nodejs
    static int startSetPubKey(eio_req *req);
    static int startSetPrvKey(eio_req *req); 
    static int startConnect(eio_req *req);
    // finishing operation after the blocking operations, these will be called
    // from the main thread, so this is the place to access v8 to return the results
    static int onDone(eio_req *req);

    virtual void freeSessions();
    // v8 is not accessible from the worker threads, so any data we need for the blocking operations, we must 
    // store elsewhere, this function is for resetting these data members between two operation
    virtual void resetData();
public:
    SSHBase(const Arguments &);
    virtual ~SSHBase();
    
    static void Initialize(Handle<Object> &);

    // exported functions
    static Handle<Value> init(const Arguments &args);
    static Handle<Value> connect(const Arguments &args);
    static Handle<Value> interrupt(const Arguments &args);
    static Handle<Value> setPubKey(const Arguments &args);
    static Handle<Value> setPrvKey(const Arguments &args);    
};

// helper functions
void setOption(ssh_session& session, Local<Object>& obj, const char* prop_name, 
                ssh_options_e opt); 
void setMember(char*& member, Local<Object>& obj, const char* prop_name);
void setMember(int& member, Local<Object>& obj, const char* prop_name);
void setCharData(char*& to, Local<Value> data);
Handle<Value> createBuffer(char* data, size_t size);

#endif