#include "CollectiveRepresentation.h"
#include <map>
#include <string>
#include <vector>

#include "../utils/Collectives.h"

using namespace std;

CollectiveRepresentation::CollectiveRepresentation() : associated_name() {
}

CollectiveRepresentation::~CollectiveRepresentation() {
}

inline void CollectiveRepresentation::associate(string const& mpi_name, string const& new_name) {
    associated_name[mpi_name] = new_name;
}

string const& CollectiveRepresentation::operator[](string const& in) {
    return this->associated_name[in];
}

void CollectiveRepresentation::init() {
    for(auto mpi_name: MPI_v_coll) {
        string mpi_cpp = mpi_name;
        associate(mpi_name, mpi_cpp.substr(mpi_cpp.find("_")+1));
    }
}