#include "algorithms/Dijkstra.h"
#include "data_structures/MaxHeap.h"
#include "models/Reservation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        Graph graph;
        GraphVertex entrance;
        entrance.id = 2;
        entrance.label = "Entrance";
        GraphVertex junction;
        junction.id = 9;
        junction.label = "Junction";
        GraphVertex room;
        room.id = 17;
        room.label = "Room";
        GraphVertex isolated;
        isolated.id = 30;
        isolated.label = "Isolated";
        require(graph.addVertex(entrance) && graph.addVertex(junction) &&
                graph.addVertex(room) && graph.addVertex(isolated), "Graph vertex insertion failed");
        require(graph.addEdge(2, 9, 4) && graph.addEdge(9, 17, 3) &&
                graph.addEdge(2, 17, 10), "Graph edge insertion failed");
        require(!graph.addEdge(2, 9, 4), "Duplicate edge should be rejected");
        require(graph.validate(), "Graph should be symmetric and valid");

        PathResult shortest = dijkstra(graph, 2, 17);
        require(shortest.path == std::vector<int>({2, 9, 17}) && shortest.distance == 7,
                "Dijkstra should choose the least-cost route for sparse IDs");
        PathResult unreachable = dijkstra(graph, 2, 30);
        require(unreachable.path.empty() && std::isinf(unreachable.distance),
                "Disconnected destination should have no route");

        graph.updateEdge(9, 17, 12, "STAIRS", "STAIRS", true);
        require(dijkstra(graph, 2, 17).path == std::vector<int>({2, 17}),
                "Updated edge weight should change the shortest path");
        graph.setVertexActive(17, false);
        require(dijkstra(graph, 2, 17).path.empty(), "Inactive vertices must not be routed through");
        require(graph.removeVertex(9) && graph.validate(), "Removing a vertex should remove its reciprocal edges");

        MaxHeap<int> heap;
        heap.build({3, 1, 8, 2, 5});
        require(heap.size() == 5 && heap.peek() == 8, "Heap build and peek failed");
        heap.push(9);
        require(heap.pop() == 9 && heap.pop() == 8, "Heap push/pop order failed");
        heap.clear();
        require(heap.empty(), "Heap clear failed");

        MaxHeap<ReservationQueueItem> queue;
        queue.build({{3, 1, 1, "2026-01-02 10:00:00"},
                     {2, 1, 2, "2026-01-03 10:00:00"},
                     {1, 2, 2, "2026-01-01 10:00:00"}});
        require(queue.pop().requestId == 1 && queue.pop().requestId == 2 &&
                queue.pop().requestId == 3, "Reservation heap should order priority, then FIFO, then request ID");
        std::cout << "Graph and heap tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Graph and heap tests failed: " << error.what() << '\n';
        return 1;
    }
}
