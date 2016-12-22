#ifndef _OBJECT_H_
#define _OBJECT_H_

class CObject
{
    unsigned long m_ulReferences;

protected:
    CObject();
    virtual ~CObject();  // this has to be virtual, otherwise derived-class destructors will not be called

public:
    void Acquire();
    void Release();
};

#endif  /* _OBJECT_H_ */
