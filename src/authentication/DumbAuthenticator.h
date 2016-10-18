/* Copyright 2016 Joao Carreira */

#ifndef _DUMB_AUTHENTICATOR_H_
#define _DUMB_AUTHENTICATOR_H_

namespace sirius {

class DumbAuthenticator : public Authenticator {
public:
    DumbAuthenticator(){}
    bool allowApplication(const ApplicationKey& app_key) { 
        return true; 
    }
private:
};

} // sirius

#endif // _DUMB_AUTHENTICATOR_H_