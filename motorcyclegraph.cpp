#include "motorcyclegraph.h"

#define foreach BOOST_FOREACH

MotorcycleGraph::MotorcycleGraph(MotorcycleConstants::MyMesh& polymesh) :
    V("V"),
    M("M"),
    E("E"),
    F("F"),
    v_manager(polymesh, this->V),
    m_manager(polymesh, this->M),
    e_manager(polymesh, this->E),
    f_manager(polymesh, this->F),
    polymesh(polymesh) {


    this->polymesh.request_face_colors();

    for (MotorcycleConstants::MyMesh::EdgeIter e_iter = this->polymesh.edges_begin(); e_iter != this->polymesh.edges_end(); ++e_iter) {
        this->e_manager[*e_iter] = this->polymesh.is_boundary(*e_iter);
    }

    for (MotorcycleConstants::MyMesh::FaceIter f_iter = this->polymesh.faces_begin(); f_iter != this->polymesh.faces_end(); ++f_iter) {
        this->f_manager[*f_iter] = SIZE_T_MAX;
    }

    Motorcycles motorcycles;
    int motorcycle_count = 0;
    for (MotorcycleConstants::MyMesh::VertexIter v_iter = this->polymesh.vertices_begin(); v_iter != this->polymesh.vertices_end(); ++v_iter) {
        bool is_extraordinary = !this->is_ordinary(*v_iter);
        this->v_manager[*v_iter] = is_extraordinary || this->polymesh.is_boundary(*v_iter);
        this->m_manager[*v_iter] = is_extraordinary;
        // TODO: add face assignment here
        motorcycle_count += is_extraordinary;
        if (is_extraordinary) {
            for (MotorcycleConstants::MyMesh::VertexOHalfedgeIter h_iter = this->polymesh.voh_begin(*v_iter); h_iter != this->polymesh.voh_end(*v_iter); ++h_iter) {
                HalfedgeHandle halfedge = *h_iter;

                EdgeHandle edge = this->polymesh.edge_handle(halfedge);
                this->e_manager[edge] = true;

                assert(motorcycles.count(halfedge.idx()) == 0);
                motorcycles.emplace(halfedge.idx(), std::unique_ptr<Motorcycle>(new Motorcycle(this->polymesh, halfedge)));
            }
        }
    }

    std::cout << "# Extraordinary = " << motorcycle_count << std::endl;
    std::cout << "# Vertices = " << this->polymesh.n_vertices() << std::endl;
    std::cout << "# Faces = " << this->polymesh.n_faces() << std::endl;
    std::cout << "# Edges = " << this->polymesh.n_edges() << std::endl;
    std::cout << std::endl;

    this->propagate_motorcycles(motorcycles);
    int n_vertices = 0;
    for (const auto& v : this->polymesh.vertices()) {
        if (this->v_manager[v]) {
            n_vertices++;
        }
    }
    std::cout << "Motorcycle graph contains " << n_vertices << " vertices after propagation." << std::endl;

    int n_edges = 0;
    for (const auto& e : this->polymesh.edges()) {
        if (this->e_manager[e]) {
            n_edges++;
        }
    }
    std::cout << "Motorcycle graph contains " << n_edges << " edges after propagation." << std::endl;
    std::cout << std::endl;

    std::vector<Perimeter> perimeters = this->extract_perimeters();
    std::cout << "Extracted " << perimeters.size() << " perimeters." << std::endl;
    std::cout << std::endl;

    this->assign_patches(perimeters);

}

/*
 * Returns whether a vertex handle is one of...
 *   - a non-boundary vertex incident with four edges
 *   - a boundary vertex incident with at most three edges
 *
 */
bool MotorcycleGraph::is_ordinary(VertexHandle v) {
    int out_degree = 0;
    for (MotorcycleConstants::MyMesh::VertexVertexIter iter = this->polymesh.vv_begin(v); iter != this->polymesh.vv_end(v); ++iter) {
        out_degree++;
    }
    bool non_boundary_quad = !this->polymesh.is_boundary(v) && out_degree == 4;
    bool boundary_sub_quad = this->polymesh.is_boundary(v) && out_degree < 4;
    return non_boundary_quad or boundary_sub_quad;
}

void MotorcycleGraph::propagate_motorcycles(Motorcycles& motorcycles) {
    std::cout << "Propagating motorcycles." << std::endl;
    std::unordered_set<int> v_seen{};

    // go until every motorcycle has crashed
    while (!motorcycles.empty()) {

        // build positions: map of vertex index to set of halfedge indices
        // which uniquely define the origin of some motorcycles
        std::unordered_map<int,  std::unordered_set<int> > positions;
        foreach (int origin, motorcycles | boost::adaptors::map_keys) {
            std::unique_ptr<Motorcycle>& motorcycle = motorcycles.at(origin);
            HalfedgeHandle h_curr = motorcycle->step();
            this->e_manager[this->polymesh.edge_handle(h_curr)] = true;
            VertexHandle v_curr = this->polymesh.to_vertex_handle(h_curr);
            positions[v_curr.idx()].insert(motorcycle->origin);
        }

        // handle the event at every location one by one, removing
        // motorcycles which have crashed from the map
        foreach (int position, positions | boost::adaptors::map_keys) {
            VertexHandle v_curr = this->polymesh.vertex_handle(position);
            bool seen = v_seen.count(position) == 1;
            bool is_boundary = this->polymesh.is_boundary(v_curr);
            bool is_tertiary = positions[position].size() > 2;
            bool is_extraordinary = !this->is_ordinary(v_curr);
            if (seen || is_boundary || is_tertiary || is_extraordinary) {
                this->v_manager[v_curr] = true;
                for (const int& origin : positions[position]) {
                    motorcycles.erase(origin);
                }
            } else if (positions[position].size() == 2) {
                std::unordered_set<int>::iterator m_iter = positions[v_curr.idx()].begin();
                std::unique_ptr<Motorcycle>& m1 = motorcycles.at(*m_iter++);
                std::unique_ptr<Motorcycle>& m2 = motorcycles.at(*m_iter++);
                if (m1->next() == this->polymesh.opposite_halfedge_handle(m2->curr())) {
                    for (const int& origin : positions[position]) {
                        motorcycles.erase(origin);
                    }
                } else {
                    this->v_manager[v_curr] = true;
                    int m1_next_out = this->polymesh.next_halfedge_handle(m1->curr()).idx();
                    int m2_opposite = this->polymesh.opposite_halfedge_handle(m2->curr()).idx();
                    if (m1_next_out == m2_opposite) {
                        motorcycles.erase(m2->origin);
                    } else {
                        motorcycles.erase(m1->origin);
                    }
                }
            }
        }

        // update the set we use to keep track of seen vertex indices
        foreach (int position, positions | boost::adaptors::map_keys) {
            v_seen.insert(position);
        }

    }

}

std::vector<MotorcycleGraph::Perimeter> MotorcycleGraph::extract_perimeters() {
    std::cout << "Extracting perimeters." << std::endl;

    // Build the set of seeds from which to start the
    // follow-your-nose search
    std::vector<std::pair<int, int> > seeds;
    for (const VertexHandle& v_handle : this->polymesh.vertices()) {
        if (this->v_manager[v_handle]) {
            MotorcycleConstants::MyMesh::VertexOHalfedgeCWIter iter = this->polymesh.voh_cwbegin(v_handle);
            while  (iter != this->polymesh.voh_cwend(v_handle)) {
                bool is_boundary = this->polymesh.is_boundary(*iter);
                bool is_edge  = e_manager[this->polymesh.edge_handle(*iter)];
                if (!is_boundary && is_edge) {
                    seeds.push_back(std::make_pair(v_handle.idx(), iter->idx()));
                }
                ++iter;
            }
        }
    }

    // Follow your nose !
    // TODO: improve comment
    std::unordered_set<int> h_seen{};
    std::vector<Perimeter> perimeters;
    for (const std::pair<int, int>& seed : seeds) {
        h_seen.insert(seed.second);
        VertexHandle v_seed = this->polymesh.vertex_handle(seed.first);
        VertexHandle v_curr = v_seed;
        HalfedgeHandle h_curr = this->polymesh.halfedge_handle(seed.second);
        bool good_seed = true;
        Perimeter perimeter{std::make_pair(v_seed.idx(), std::vector<int>{h_curr.idx()})};
        while (this->polymesh.to_vertex_handle(h_curr) != v_seed) {
            std::vector<int> interior_halfedges;
            HalfedgeHandle temp = this->polymesh.next_halfedge_handle(h_curr);
            while (true) {
                interior_halfedges.push_back(temp.idx());
                if (e_manager[this->polymesh.edge_handle(temp)]) {
                    break;
                }
                temp = this->polymesh.opposite_halfedge_handle(temp);
                temp = this->polymesh.next_halfedge_handle(temp);
            }
            assert(temp != this->polymesh.opposite_halfedge_handle(h_curr));
            v_curr = this->polymesh.to_vertex_handle(h_curr);
            h_curr = temp;
            if (h_seen.count(h_curr.idx()) == 1) {
                good_seed = false;
                break;
            }
            h_seen.insert(h_curr.idx());
            perimeter.push_back(std::make_pair(v_curr.idx(), interior_halfedges));
        }
        if (good_seed) {
            perimeters.push_back(perimeter);
        }
    }

    return perimeters;

}

void MotorcycleGraph::assign_patches(std::vector<Perimeter> &perimeters1) {
    std::cout << "Assigning patches." << std::endl;

    std::vector<Perimeter> perimeters = MotorcycleGraph::sort_perimeters(perimeters1);

    size_t patch_id = 0;
    for (const Perimeter& perimeter : perimeters) {
        std::vector<int> to_explore;
        size_t j = 0;
        for (const std::pair<int, std::vector<int> >& p : perimeter) {
            std::vector<int> halfedges = p.second;
            int v_next;
            try {
                v_next = perimeter[j + 1].first;
            } catch (std::out_of_range) {
                v_next = perimeter[0].first;
            }
            for (const int& h : halfedges) {
                HalfedgeHandle h_handle = this->polymesh.halfedge_handle(h);
                if (this->polymesh.to_vertex_handle(h_handle).idx() != v_next) {
                    to_explore.push_back(h);
                }
            }
            j++;
        }

        std::unordered_set<int> boundary;
        for (const std::pair<int, std::vector<int> >& p : perimeter) {
            boundary.insert(p.first);
        }

        std::queue<int> queue;
        for (const int& h : to_explore) {
            HalfedgeHandle h_handle = this->polymesh.halfedge_handle(h);
            queue.push(this->polymesh.to_vertex_handle(h_handle).idx());
        }

        std::unordered_set<int> v_seen;
        while (!queue.empty()) {
            VertexHandle v_curr = this->polymesh.vertex_handle(queue.front());
            queue.pop();
            if (true) { // v_seen.count(v_curr.idx()) == 0) {// && boundary.count(v_curr.idx()) == 0) {
                v_seen.insert(v_curr.idx());
                MotorcycleConstants::MyMesh::VertexOHalfedgeCWIter iter = this->polymesh.voh_cwbegin(v_curr);
                while  (iter != this->polymesh.voh_cwend(v_curr)) {
                    VertexHandle v_next = this->polymesh.to_vertex_handle(*iter);
                    bool on_boundary = false; //boundary.count(v_next.idx()) == 1;
                    if (!on_boundary) {
                        FaceHandle f_handle = this->polymesh.face_handle(*iter);
                        if (f_handle.is_valid() && this->f_manager[f_handle] == SIZE_T_MAX) {
                            this->f_manager[f_handle] = patch_id;
                        }
                        // TODO: this if *should* be unnecessary
                        if (v_seen.count(v_next.idx()) == 0) {
                            v_seen.insert(v_next.idx());
                        } else {
                            std::cout << " error";
                        }
                    }
                    ++iter;
                }
            }
        }
        patch_id++;
    }
}

void MotorcycleGraph::save_mesh() {
    for (const FaceHandle& f_handle : this->polymesh.faces()) {
        if (f_handle.is_valid()) {
            int patch_id = this->f_manager[f_handle];
            std::srand(patch_id);
            int r = std::rand() % 255;
            std::srand(r);
            int g = std::rand() % 255;
            std::srand(g);
            int b = std::rand() % 255;
            this->polymesh.set_color(f_handle, MotorcycleConstants::MyMesh::Color(r, g, b));
        }
    }
    IO::Options opt;
    opt += IO::Options::FaceColor;
    std::string fname =  "/Users/jkamalu/Documents/Masters/Bipolar/test.obj";
    if (!IO::write_mesh(this->polymesh, fname, opt)) {
        std::cerr << "Error" << std::endl;
        std::cerr << "Possible reasons:\n";
        std::cerr << "1. Chosen format cannot handle an option!\n";
        std::cerr << "2. Mesh does not provide necessary information!\n";
        std::cerr << "3. Or simply cannot open file for writing!\n";
    } else
        std::cout << "Mesh saved to " << fname << std::endl;
}
