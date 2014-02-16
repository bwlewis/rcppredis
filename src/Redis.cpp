// -*- indent-tabs-mode: nil; tab-width: 4; c-indent-level: 4; c-basic-offset: 4; -*-
//
// simple C++ class to host a stateful connection to redis
//
// uses hiredis library which provides a basic C API to redis
//
// initially forked from Wush Wu's Rhiredis
// slowly adding some more Redis functions
//
// Dirk Eddelbuettel, 2013 - 2014

#include <Rcpp.h>
#include <hiredis/hiredis.h>         // on Ubuntu file /usr/include/hiredis/hiredis.h

extern "C" SEXP serializeToRaw(SEXP object);
extern "C" SEXP unserializeFromRaw(SEXP object);


// A simple and lightweight class -- with just a simple private member variable 
// We could add some more member variables to cache the last call, status, ...
//

class Redis {

private: 
   
    redisContext *prc_;                // private pointer to redis context

    void init(std::string host="127.0.0.1", int port=6379)  { 
        prc_ = redisConnect(host.c_str(), port);
        if (prc_->err) 
            Rcpp::stop(std::string("Redis connection error: ") + std::string(prc_->errstr));
    }
	
    SEXP extract_reply(redisReply *reply){
        switch(reply->type) {
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS: {
            std::string res(reply->str);
            return(Rcpp::wrap(res));
        }
        case REDIS_REPLY_INTEGER: {
            return(Rcpp::wrap(static_cast<double>(reply->integer)));
        }
        case REDIS_REPLY_ERROR: {
            std::string res(reply->str);
            return(Rcpp::wrap(res));
        }
        case REDIS_REPLY_NIL: {
            return(R_NilValue);
        }
        case REDIS_REPLY_ARRAY: {
            Rcpp::List retlist(reply->elements);
            extract_array(reply, retlist);
            return(retlist);
        }
        default:
            throw std::logic_error("Unknown type");
        }
    }
    
    void extract_array(redisReply *node, Rcpp::List& retlist) {
        for(unsigned int i = 0;i < node->elements;i++) {
            retlist[i] = extract_reply(node->element[i]);
        }
    }

public:
   
    Redis(std::string host, int port)  { init(host, port); }
    Redis(std::string host)            { init(host);       }
    Redis()                            { init();           }

    ~Redis() { 
        redisFree(prc_);
        prc_ = NULL;                // just to be on the safe side
    }

    // execute given string
    SEXP exec(std::string cmd) {
        redisReply *reply = static_cast<redisReply*>(redisCommand(prc_, cmd.c_str()));
        SEXP rep = extract_reply(reply);
        freeReplyObject(reply);
        return(rep);
    }


    // redis set -- serializes to R internal format
    std::string set(std::string key, SEXP s) {

        // if raw, use as is else serialize to raw
        Rcpp::RawVector x = (TYPEOF(s) == RAWSXP) ? s : serializeToRaw(s);

        // uses binary protocol, see hiredis doc at github
        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "SET %s %b", 
                                                  key.c_str(), x.begin(), x.size()));
        std::string res(reply->str);                                                
        freeReplyObject(reply);
        return(res);
    }

    // redis get -- deserializes from R format
    SEXP get(std::string key) {

        // uses binary protocol, see hiredis doc at github
        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "GET %s", key.c_str()));

        int nc = reply->len;
        SEXP res = Rf_allocVector(RAWSXP, nc);
        memcpy(RAW(res), reply->str, nc);
                                               
        freeReplyObject(reply);
        SEXP obj = unserializeFromRaw(res);
        return(obj);
    }

    // redis keys -- returns character vector
    SEXP keys(std::string regexp) {

        // uses binary protocol, see hiredis doc at github
        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "KEYS %s", regexp.c_str()));

        unsigned int nc = reply->elements;
        Rcpp::CharacterVector vec(nc);
        for (unsigned int i = 0; i < nc; i++) {
            vec[i] = reply->element[i]->str;
        }
        freeReplyObject(reply);
        return(vec);
    }

    // could create new functions to (re-)connect with given host and port etc pp

    
    // used in functions below
    static const unsigned int szdb = sizeof(double);

    // redis "set a vector" -- without R serialization, without attributes, ...
    // this is somewhat experimental
    std::string setVector(std::string key, Rcpp::NumericVector x) {

        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "SET %s %b", 
                                                  key.c_str(), x.begin(), x.size()*szdb));
        std::string res(reply->str);                                                
        freeReplyObject(reply);
        return(res);
    }

    // redis "get a vector" -- without R serialization, without attributes, ...
    // this is somewhat experimental
    Rcpp::NumericVector getVector(std::string key) {

        // uses binary protocol, see hiredis doc at github
        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "GET %s", key.c_str()));

        int nc = reply->len;
        Rcpp::NumericVector x(nc/szdb);
        memcpy(x.begin(), reply->str, nc);
                                               
        freeReplyObject(reply);
        return(x);
    }



    // redis "get from list from start to end" -- with R serialization
    Rcpp::List lrange(std::string key, int start, int end) {

        // uses binary protocol, see hiredis doc at github
        redisReply *reply = 
            static_cast<redisReply*>(redisCommand(prc_, "LRANGE %s %d %d", 
                                                  key.c_str(), start, end));

        unsigned int len = reply->elements;
        //Rcpp::Rcout << "Seeing " << len << " elements\n";
        Rcpp::List x(len);
        for (unsigned int i = 0; i < len; i++) {
            //Rcpp::Rcout << "  Seeing size " << reply->element[i]->len << "\n";
            int nc = reply->element[i]->len;
            SEXP res = Rf_allocVector(RAWSXP, nc);
            memcpy(RAW(res), reply->element[i]->str, nc);
            SEXP obj = unserializeFromRaw(res);
            x[i] = obj;
        }
                                               
        freeReplyObject(reply);
        return(x);
    }


};


RCPP_MODULE(Redis) {
    Rcpp::class_<Redis>("Redis")   
        
        .constructor("default constructor")  
        .constructor<std::string>("constructor with host port")  
        .constructor<std::string, int>("constructor with host and port")  

        .method("exec", &Redis::exec,  "execute given redis command and arguments")

        .method("set",  &Redis::set,   "runs 'SET key object', serializes internally")
        .method("get",  &Redis::get,   "runs 'GET key', deserializes internally")
        .method("keys", &Redis::keys,  "runs 'KEYS expr', returns character vector")

        .method("setVector",  &Redis::setVector,   "runs 'SET key object' for a numeric vector")
        .method("getVector",  &Redis::getVector,   "runs 'SET key object' for a numeric vector")

        .method("lrange",  &Redis::lrange,   "runs 'LRANGE key start end' for list")
    ;
}



