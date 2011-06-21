/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */

#ifndef NODE_SSH_TUNNEL_H
#define NODE_SSH_TUNNEL_H

#include <node.h>
#include <node_object_wrap.h>
#include <node_events.h>
#include <v8.h>

#include <libssh/libssh.h>

#include "sshBase.h"

using namespace v8;
using namespace node;


class Tunnel : public SSHBase {
private:
    ssh_channel m_ssh_channel;
    char* m_remote_host;
    int   m_remote_port;
    char* m_src_host;
    int   m_local_port;
    char* m_rdata;
    size_t m_size;
    size_t m_pos;
  
    static Persistent<FunctionTemplate> constructor_template;
    
    static int startConnect(eio_req *req);
    static int startWrite(eio_req *req);
    static int startRead(eio_req *req);
    static int startSetPubKey(eio_req *req);
    static int startSetPrvKey(eio_req *req);    
    static int onRead(eio_req *req);
    
    virtual void freeSessions();
    virtual void resetData();
protected:
    static Handle<Value> New(const Arguments &args);

public:
    Tunnel(const Arguments &);
    virtual ~Tunnel();
    
    static void Initialize(Handle<Object> &);
    
    static Handle<Value> init(const Arguments &args);
    static Handle<Value> connect(const Arguments &args);
    static Handle<Value> write(const Arguments &args);
    static Handle<Value> read(const Arguments &args); 
};

#endif