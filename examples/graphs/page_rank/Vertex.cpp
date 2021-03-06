#include <examples/graphs/page_rank/Vertex.h>
#include <arpa/inet.h>

namespace graphs {

Vertex::Vertex(int id) :
    id(id), p_curr(1.0), p_next(0.0) {
}

Vertex::Vertex(int id, const std::vector<int>& neighbors) :
    id(id), p_curr(1.0), p_next(0.0) {
    setNeighbors(neighbors);
}

void Vertex::setNeighbors(const std::vector<int>& v) {
    for (const auto& i : v) {
        addNeighbor(i);
    }
}

void Vertex::addNeighbor(int id) {
    neighbors.insert(id);
}

std::set<int> Vertex::getNeighbors() const {
    return neighbors;
}

uint64_t Vertex::getNeighborsSize() const {
    return neighbors.size();
}

int Vertex::getId() const {
    return id;
}

void Vertex::setId(int i) {
    id = i;
}

bool Vertex::hasNeighbor(int id) const {
    return neighbors.find(id) != neighbors.end();
}

Vertex Vertex::deserializer(const void* data, unsigned int size) {
    const double* double_ptr = reinterpret_cast<const double*>(data);

    Vertex v;
    v.setCurrProb(*double_ptr++);
    v.setNextProb(*double_ptr++);

    std::cout << "deserialized with currProb: " << v.getCurrProb()
        << " nextProb: " << v.getNextProb()
        << std::endl;

    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(double_ptr);
    v.setId(ntohl(*ptr++));
    uint32_t n = ntohl(*ptr++);

    uint32_t expected_size = 2 * sizeof(double) + sizeof(uint32_t) * (2 + n);

    if (size != expected_size) {
        throw std::runtime_error("Incorrect size: "
                + std::to_string(size) +
                + " expected: " + std::to_string(expected_size));
    }

    for (uint32_t i = 0; i < n; ++i) {
        v.addNeighbor(ntohl(*ptr++));
    }
    return v;
}

double Vertex::getCurrProb() const {
    return p_curr;
}

void Vertex::setCurrProb(double prob) {
    p_curr = prob;
}

double Vertex::getNextProb() const {
    return p_next;
}

void Vertex::setNextProb(double prob) {
    p_next = prob;
}

void Vertex::print() const {
    std::cout
        << "Print vertex id: " << id
        << " p_next: " << p_next
        << " p_curr: " << p_curr
        << std::endl;
}

}  // namespace graphs
