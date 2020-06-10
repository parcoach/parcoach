#ifndef COLL_REPR_H
#define COLL_REPR_H

#include <string>
#include <map>

class CollectiveRepresentation
{
private:
    std::map<std::string, std::string> associated_name;
    inline void associate(std::string const&, std::string const&);
public:
    CollectiveRepresentation();
    ~CollectiveRepresentation();

    void init();

    std::string const& operator[](std::string const& s) ;
};

#endif//COLL_REPR_H