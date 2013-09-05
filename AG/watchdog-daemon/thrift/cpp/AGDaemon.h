/**
 * Autogenerated by Thrift Compiler (0.8.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#ifndef AGDaemon_H
#define AGDaemon_H

#include <TProcessor.h>
#include "ag_daemon_types.h"

namespace watchdog {

class AGDaemonIf {
 public:
  virtual ~AGDaemonIf() {}
  virtual int32_t restart(const int32_t ag_id) = 0;
  virtual void ping() = 0;
};

class AGDaemonIfFactory {
 public:
  typedef AGDaemonIf Handler;

  virtual ~AGDaemonIfFactory() {}

  virtual AGDaemonIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) = 0;
  virtual void releaseHandler(AGDaemonIf* /* handler */) = 0;
};

class AGDaemonIfSingletonFactory : virtual public AGDaemonIfFactory {
 public:
  AGDaemonIfSingletonFactory(const boost::shared_ptr<AGDaemonIf>& iface) : iface_(iface) {}
  virtual ~AGDaemonIfSingletonFactory() {}

  virtual AGDaemonIf* getHandler(const ::apache::thrift::TConnectionInfo&) {
    return iface_.get();
  }
  virtual void releaseHandler(AGDaemonIf* /* handler */) {}

 protected:
  boost::shared_ptr<AGDaemonIf> iface_;
};

class AGDaemonNull : virtual public AGDaemonIf {
 public:
  virtual ~AGDaemonNull() {}
  int32_t restart(const int32_t /* ag_id */) {
    int32_t _return = 0;
    return _return;
  }
  void ping() {
    return;
  }
};

typedef struct _AGDaemon_restart_args__isset {
  _AGDaemon_restart_args__isset() : ag_id(false) {}
  bool ag_id;
} _AGDaemon_restart_args__isset;

class AGDaemon_restart_args {
 public:

  AGDaemon_restart_args() : ag_id(0) {
  }

  virtual ~AGDaemon_restart_args() throw() {}

  int32_t ag_id;

  _AGDaemon_restart_args__isset __isset;

  void __set_ag_id(const int32_t val) {
    ag_id = val;
  }

  bool operator == (const AGDaemon_restart_args & rhs) const
  {
    if (!(ag_id == rhs.ag_id))
      return false;
    return true;
  }
  bool operator != (const AGDaemon_restart_args &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const AGDaemon_restart_args & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};


class AGDaemon_restart_pargs {
 public:


  virtual ~AGDaemon_restart_pargs() throw() {}

  const int32_t* ag_id;

  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};

typedef struct _AGDaemon_restart_result__isset {
  _AGDaemon_restart_result__isset() : success(false) {}
  bool success;
} _AGDaemon_restart_result__isset;

class AGDaemon_restart_result {
 public:

  AGDaemon_restart_result() : success(0) {
  }

  virtual ~AGDaemon_restart_result() throw() {}

  int32_t success;

  _AGDaemon_restart_result__isset __isset;

  void __set_success(const int32_t val) {
    success = val;
  }

  bool operator == (const AGDaemon_restart_result & rhs) const
  {
    if (!(success == rhs.success))
      return false;
    return true;
  }
  bool operator != (const AGDaemon_restart_result &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const AGDaemon_restart_result & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};

typedef struct _AGDaemon_restart_presult__isset {
  _AGDaemon_restart_presult__isset() : success(false) {}
  bool success;
} _AGDaemon_restart_presult__isset;

class AGDaemon_restart_presult {
 public:


  virtual ~AGDaemon_restart_presult() throw() {}

  int32_t* success;

  _AGDaemon_restart_presult__isset __isset;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);

};


class AGDaemon_ping_args {
 public:

  AGDaemon_ping_args() {
  }

  virtual ~AGDaemon_ping_args() throw() {}


  bool operator == (const AGDaemon_ping_args & /* rhs */) const
  {
    return true;
  }
  bool operator != (const AGDaemon_ping_args &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const AGDaemon_ping_args & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};


class AGDaemon_ping_pargs {
 public:


  virtual ~AGDaemon_ping_pargs() throw() {}


  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};


class AGDaemon_ping_result {
 public:

  AGDaemon_ping_result() {
  }

  virtual ~AGDaemon_ping_result() throw() {}


  bool operator == (const AGDaemon_ping_result & /* rhs */) const
  {
    return true;
  }
  bool operator != (const AGDaemon_ping_result &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const AGDaemon_ping_result & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};


class AGDaemon_ping_presult {
 public:


  virtual ~AGDaemon_ping_presult() throw() {}


  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);

};

class AGDaemonClient : virtual public AGDaemonIf {
 public:
  AGDaemonClient(boost::shared_ptr< ::apache::thrift::protocol::TProtocol> prot) :
    piprot_(prot),
    poprot_(prot) {
    iprot_ = prot.get();
    oprot_ = prot.get();
  }
  AGDaemonClient(boost::shared_ptr< ::apache::thrift::protocol::TProtocol> iprot, boost::shared_ptr< ::apache::thrift::protocol::TProtocol> oprot) :
    piprot_(iprot),
    poprot_(oprot) {
    iprot_ = iprot.get();
    oprot_ = oprot.get();
  }
  boost::shared_ptr< ::apache::thrift::protocol::TProtocol> getInputProtocol() {
    return piprot_;
  }
  boost::shared_ptr< ::apache::thrift::protocol::TProtocol> getOutputProtocol() {
    return poprot_;
  }
  int32_t restart(const int32_t ag_id);
  void send_restart(const int32_t ag_id);
  int32_t recv_restart();
  void ping();
  void send_ping();
  void recv_ping();
 protected:
  boost::shared_ptr< ::apache::thrift::protocol::TProtocol> piprot_;
  boost::shared_ptr< ::apache::thrift::protocol::TProtocol> poprot_;
  ::apache::thrift::protocol::TProtocol* iprot_;
  ::apache::thrift::protocol::TProtocol* oprot_;
};

class AGDaemonProcessor : public ::apache::thrift::TProcessor {
 protected:
  boost::shared_ptr<AGDaemonIf> iface_;
  virtual bool process_fn(apache::thrift::protocol::TProtocol* iprot, apache::thrift::protocol::TProtocol* oprot, std::string& fname, int32_t seqid, void* callContext);
 private:
  std::map<std::string, void (AGDaemonProcessor::*)(int32_t, apache::thrift::protocol::TProtocol*, apache::thrift::protocol::TProtocol*, void*)> processMap_;
  void process_restart(int32_t seqid, apache::thrift::protocol::TProtocol* iprot, apache::thrift::protocol::TProtocol* oprot, void* callContext);
  void process_ping(int32_t seqid, apache::thrift::protocol::TProtocol* iprot, apache::thrift::protocol::TProtocol* oprot, void* callContext);
 public:
  AGDaemonProcessor(boost::shared_ptr<AGDaemonIf> iface) :
    iface_(iface) {
    processMap_["restart"] = &AGDaemonProcessor::process_restart;
    processMap_["ping"] = &AGDaemonProcessor::process_ping;
  }

  virtual bool process(boost::shared_ptr<apache::thrift::protocol::TProtocol> piprot, boost::shared_ptr<apache::thrift::protocol::TProtocol> poprot, void* callContext);
  virtual ~AGDaemonProcessor() {}
};

class AGDaemonProcessorFactory : public ::apache::thrift::TProcessorFactory {
 public:
  AGDaemonProcessorFactory(const ::boost::shared_ptr< AGDaemonIfFactory >& handlerFactory) :
      handlerFactory_(handlerFactory) {}

  ::boost::shared_ptr< ::apache::thrift::TProcessor > getProcessor(const ::apache::thrift::TConnectionInfo& connInfo);

 protected:
  ::boost::shared_ptr< AGDaemonIfFactory > handlerFactory_;
};

class AGDaemonMultiface : virtual public AGDaemonIf {
 public:
  AGDaemonMultiface(std::vector<boost::shared_ptr<AGDaemonIf> >& ifaces) : ifaces_(ifaces) {
  }
  virtual ~AGDaemonMultiface() {}
 protected:
  std::vector<boost::shared_ptr<AGDaemonIf> > ifaces_;
  AGDaemonMultiface() {}
  void add(boost::shared_ptr<AGDaemonIf> iface) {
    ifaces_.push_back(iface);
  }
 public:
  int32_t restart(const int32_t ag_id) {
    size_t sz = ifaces_.size();
    for (size_t i = 0; i < sz; ++i) {
      if (i == sz - 1) {
        return ifaces_[i]->restart(ag_id);
      } else {
        ifaces_[i]->restart(ag_id);
      }
    }
  }

  void ping() {
    size_t sz = ifaces_.size();
    for (size_t i = 0; i < sz; ++i) {
      ifaces_[i]->ping();
    }
  }

};

} // namespace

#endif