#include <math.h>
#include <stdlib.h>
#include <Python.h>

#define INF 1000000


/* DataPoint */

static int DataPoint_current_dim = 0;

struct DataPoint
{
    long int _index;
    float *_coord;
};

static int compare(const void* self, const void* other)
{
    const struct DataPoint* p = self;
    const struct DataPoint* q = other;
    const float a = p->_coord[DataPoint_current_dim];
    const float b = q->_coord[DataPoint_current_dim];
    if (a < b) return -1;
    if (a > b) return +1;
    return 0;
}

static void DataPoint_sort(struct DataPoint* list, int n, int i)
{
    /* set sort dimension */
    DataPoint_current_dim=i;
    qsort(list, n, sizeof(struct DataPoint), compare);
}

/* Neighbor */

struct Neighbor
{
    long int index1;
    long int index2;
    float radius;
    struct Neighbor* next;
};

/* Node */

struct Node
{
    struct Node *_left;
    struct Node *_right;
    float _cut_value;
    int _cut_dim;
    long int _start, _end;
};

static struct Node*
Node_create(float cut_value, int cut_dim, long int start, long int end)
{
    struct Node* node = malloc(sizeof(struct Node));
    if (node == NULL) return NULL;
    node->_left = NULL;
    node->_right = NULL;
    node->_cut_value = cut_value;
    node->_cut_dim = cut_dim;
    /* start and end index in _data_point_list */
    node->_start = start;
    node->_end = end;
    return node;
}

static void Node_destroy(struct Node* node)
{
    if(node==NULL) return;
    Node_destroy(node->_left);
    Node_destroy(node->_right);
    free(node);
}

static int Node_is_leaf(struct Node* node)
{
    if (node->_left==NULL && node->_right==NULL) return 1;
    else return 0;
}

/* Region */

static int Region_dim=3;

struct Region
{
    float *_left;
    float *_right;
};

static struct Region* Region_create(const float *left, const float *right)
{
    struct Region* region = malloc(sizeof(struct Region));
    if(!region) return NULL;

    region->_left= malloc(Region_dim*sizeof(float));
    region->_right= malloc(Region_dim*sizeof(float));
    if (region->_left==NULL || region->_right==NULL)
    {
        if (region->_left) free(region->_left);
        if (region->_right) free(region->_right);
        free(region);
        return NULL;
    }

    if (left==NULL || right==NULL)
    {
        /* [-INF, INF] */
        int i;
        for (i = 0; i<Region_dim; i++)
        {
            region->_left[i]=-INF;
            region->_right[i]=INF;
        }
    }
    else
    {
        int i;
        for (i = 0; i<Region_dim; i++)
        {
            region->_left[i] = left[i];
            region->_right[i] = right[i];
        }
    }
    return region;
}

static void Region_destroy(struct Region* region)
{
    if(region==NULL) return;
    if(region->_left) free(region->_left);
    if(region->_right) free(region->_right);
    free(region);
}

static int Region_encloses(struct Region* region, float *coord)
{
    int i;
    for (i = 0; i<Region_dim; i++)
    {
        if (!(coord[i] >= region->_left[i] && coord[i] <= region->_right[i]))
        {
            return 0;
        }
    }
    return 1;
}

static int
Region_test_intersect_left(struct Region* region, float split_coord, int current_dim)
{
    const float r = region->_right[current_dim];
    const float l = region->_left[current_dim];
    if (split_coord < l) return -1;
    else if (split_coord < r) return 0; /* split point in interval */
    else return +1;
}

static int
Region_test_intersect_right(struct Region* region, float split_coord, int current_dim)
{
    const float r = region->_right[current_dim];
    const float l = region->_left[current_dim];
    if (split_coord <= l) return -1;
    else if (split_coord <= r) return 0; /* split point in interval */
    else return +1;
}

static int
Region_test_intersection(struct Region* this_region, struct Region *query_region, float radius)
{
    int status=2;

    int i;
    for (i = 0; i<Region_dim; i++)
    {
        float rs = this_region->_right[i];
        float ls = this_region->_left[i];
        float rq = query_region->_right[i];
        float lq = query_region->_left[i];

        if (ls-rq>radius)
        {
            /* outside */
            return 0;
        }
        else if (lq-rs>radius)
        {
            /* outside */
            return 0;
        }
        else if (rs <= rq && ls>=lq)
        {
            /* inside (at least in dim i) */
            if (status > 2) status=2;
        }
        else
        {
            /* overlap (at least in dim i) */
            status=1;
        }
    }
    return status;
}

static struct Region*
Region_create_intersect_left(struct Region* region, float split_coord, int current_dim)
{
    struct Region* p;
    const float value = region->_right[current_dim];
    region->_right[current_dim] = split_coord;
    p = Region_create(region->_left, region->_right);
    region->_right[current_dim] = value;
    return p;
}

static struct Region*
Region_create_intersect_right(struct Region* region, float split_coord, int current_dim)
{
    struct Region* p;
    const float value = region->_left[current_dim];
    region->_left[current_dim] = split_coord;
    p = Region_create(region->_left, region->_right);
    region->_left[current_dim] = value;
    return p;
}

/* Radius */

struct Radius
{
    long int index;
    float value;
};

/* KDTree */

struct KDTree
{
    struct DataPoint* _data_point_list;
    int _data_point_list_size;
    struct Radius* _radius_list;
    struct Neighbor* _neighbor_list;
    struct Node *_root;
    struct Region *_query_region;
    long int _count;
    long int _neighbor_count;
    float _radius;
    float _radius_sq;
    float _neighbor_radius;
    float _neighbor_radius_sq;
    float *_center_coord;
    float *_coords;
    int _bucket_size;
    int dim;
};

static float KDTree_dist(float *coord1, float *coord2, int dim)
{
    /* returns the SQUARE of the distance between two points */
    int i;
    float sum= 0, dif= 0;

    for (i = 0; i<dim; i++) {
        dif = coord1[i]-coord2[i];
        sum += dif*dif;
    }
    return sum;
}

static int KDTree_report_point(struct KDTree* tree, long int index, float *coord)
{
    const float r = KDTree_dist(tree->_center_coord, coord, tree->dim);
    if (r <= tree->_radius_sq)
    {
        int n = tree->_count;
        struct Radius* p;

        p = realloc(tree->_radius_list, (n+1)*sizeof(struct Radius));
        if (p==NULL) return 0;
        /* note use of sqrt - only calculated if necessary */
        p[n].index = index;
        p[n].value = sqrt(r);
        tree->_radius_list = p;
        tree->_count++;
    }
    return 1;
}

static int KDTree_test_neighbors(struct KDTree* tree, struct DataPoint* p1, struct DataPoint* p2)
{
    const float r = KDTree_dist(p1->_coord, p2->_coord, tree->dim);
    if(r <= tree->_neighbor_radius_sq)
    {
        /* we found a neighbor pair! */
        struct Neighbor* p;
        int n;
        n = tree->_neighbor_count;
        p = realloc(tree->_neighbor_list, (n+1)*sizeof(struct Neighbor));
        if (p==NULL) return 0;

        p[n].index1 = p1->_index;
        p[n].index2 = p2->_index;
        /* note sqrt */
        p[n].radius = sqrt(r);
        tree->_neighbor_list = p;
        tree->_neighbor_count++;
    }

    return 1;
}

static int KDTree_search_neighbors_in_bucket(struct KDTree* tree, struct Node *node)
{
    long int i;
    int ok;

    for (i =node->_start; i<node->_end; i++)
    {
        struct DataPoint p1;
        long int j;

        p1 = tree->_data_point_list[i];

        for (j=i+1; j<node->_end; j++)
        {
            struct DataPoint p2;

            p2 = tree->_data_point_list[j];

            ok = KDTree_test_neighbors(tree, &p1, &p2);
            if (!ok) return 0;
        }
    }
    return 1;
}

static int KDTree_search_neighbors_between_buckets(struct KDTree* tree, struct Node *node1, struct Node *node2)
{
    long int i;
    int ok;

    for (i = node1->_start; i<node1->_end; i++)
    {
        struct DataPoint p1;
        long int j;

        p1 = tree->_data_point_list[i];

        for (j = node2->_start; j<node2->_end; j++)
        {
            struct DataPoint p2;

            p2 = tree->_data_point_list[j];

            ok = KDTree_test_neighbors(tree, &p1, &p2);
            if (!ok) return 0;
        }
    }
    return 1;
}

static int KDTree_neighbor_search_pairs(struct KDTree* tree, struct Node *down, struct Region *down_region, struct Node *up, struct Region *up_region, int depth)
{
    int down_is_leaf, up_is_leaf;
    int localdim;
    int ok = 1;

    /* if regions do not overlap - STOP */
    if (!down || !up || !down_region || !up_region)
    {
        /* STOP */
        return ok;
    }

    if (Region_test_intersection(down_region, up_region, tree->_neighbor_radius)== 0)
    {
        /* regions cannot contain neighbors */
        return ok;
    }

    /* dim */
    localdim=depth%tree->dim;

    /* are they leaves? */
    up_is_leaf=Node_is_leaf(up);
    down_is_leaf=Node_is_leaf(down);

    if (up_is_leaf && down_is_leaf)
    {
        /* two leaf nodes */
        ok = KDTree_search_neighbors_between_buckets(tree, down, up);
    }
    else
    {
        /* one or no leaf nodes */

        struct Node *up_right, *up_left, *down_left, *down_right;
        struct Region *up_left_region = NULL;
        struct Region *up_right_region = NULL;
        struct Region *down_left_region = NULL;
        struct Region *down_right_region = NULL;

        if (down_is_leaf)
        {
            down_left=down;
            /* make a copy of down_region */
            down_left_region= Region_create(down_region->_left, down_region->_right);
            if (down_left_region==NULL) ok = 0;
            down_right=NULL;
            down_right_region=NULL;
        }
        else
        {
            float cut_value;
            int intersect;

            cut_value=down->_cut_value;

            down_left=down->_left;
            down_right=down->_right;
            intersect=Region_test_intersect_left(down_region, cut_value, localdim);

            if(intersect==1)
            {
                down_left_region = Region_create(down_region->_left, down_region->_right);
                if (down_left_region==NULL) ok = 0;
            }
            else if(intersect== 0)
            {
                down_left_region = Region_create_intersect_left(down_region, cut_value, localdim);
                if (down_left_region==NULL) ok = 0;
            }
            else if(intersect==-1)
            /* intersect is -1 if no overlap */
            {
                down_left_region = NULL;
            }

            intersect=Region_test_intersect_right(down_region, cut_value, localdim);
            if(intersect==-1)
            {
                down_right_region = Region_create(down_region->_left, down_region->_right);
                if (down_right_region==NULL) ok = 0;
            }
            else if(intersect== 0)
            {
                down_right_region = Region_create_intersect_right(down_region, cut_value, localdim);
                if (down_right_region==NULL) ok = 0;
            }
            else if(intersect==+1)
            {
                down_right_region = NULL;
            }
        }

        if (up_is_leaf)
        {
            up_left=up;
            /* make a copy of up_region */
            up_left_region= Region_create(up_region->_left, up_region->_right);
            if (up_left_region==NULL) ok = 0;
            up_right=NULL;
            up_right_region=NULL;
        }
        else
        {
            float cut_value;
            int intersect;

            cut_value=up->_cut_value;

            up_left=up->_left;
            up_right=up->_right;
            intersect=Region_test_intersect_left(up_region, cut_value, localdim);

            if(intersect==1)
            {
                up_left_region = Region_create(up_region->_left, up_region->_right);
                if (up_left_region==NULL) ok = 0;
            }
            else if(intersect== 0)
            {
                up_left_region = Region_create_intersect_left(up_region, cut_value, localdim);
                if (up_left_region==NULL) ok = 0;
            }
            else if(intersect==-1)
            /* intersect is -1 if no overlap */
            {
                up_left_region = NULL;
            }

            intersect=Region_test_intersect_right(up_region, cut_value, localdim);
            if(intersect==-1)
            {
                up_right_region = Region_create(up_region->_left, up_region->_right);
                if (up_right_region==NULL) ok = 0;
            }
            else if(intersect== 0)
            {
                up_right_region = Region_create_intersect_right(up_region, cut_value, localdim);
                if (up_right_region==NULL) ok = 0;
            }
            else if(intersect==+1)
            /* intersect is +1 if no overlap */
            {
                up_right_region = NULL;
            }
        }

        if (ok)
            ok = KDTree_neighbor_search_pairs(tree, up_left, up_left_region, down_left, down_left_region, depth+1);
        if (ok)
            ok = KDTree_neighbor_search_pairs(tree, up_left, up_left_region, down_right, down_right_region, depth+1);
        if (ok)
            ok = KDTree_neighbor_search_pairs(tree, up_right, up_right_region, down_left, down_left_region, depth+1);
        if (ok)
            ok = KDTree_neighbor_search_pairs(tree, up_right, up_right_region, down_right, down_right_region, depth+1);

        Region_destroy(down_left_region);
        Region_destroy(down_right_region);
        Region_destroy(up_left_region);
        Region_destroy(up_right_region);
    }
    return ok;
}

static int KDTree__neighbor_search(struct KDTree* tree, struct Node *node, struct Region *region, int depth)
{
    struct Node *left, *right;
    struct Region *left_region = NULL;
    struct Region *right_region = NULL;
    int localdim;
    int intersect;
    float cut_value;
    int ok = 1;

    localdim=depth%tree->dim;

    left=node->_left;
    right=node->_right;

    cut_value = node->_cut_value;

    /* planes of left and right nodes */
    intersect=Region_test_intersect_left(region, cut_value, localdim);
    if(intersect==1)
    {
        left_region = Region_create(region->_left, region->_right);
        if (!left_region) ok = 0;
    }
    else if(intersect== 0)
    {
        left_region = Region_create_intersect_left(region, cut_value, localdim);
        if (!left_region) ok = 0;
    }
    else if(intersect==-1)
    /* intersect is -1 if no overlap */
    {
        left_region = NULL;
    }

    intersect=Region_test_intersect_right(region, cut_value, localdim);
    if(intersect==-1)
    {
        right_region = Region_create(region->_left, region->_right);
        if (!right_region) ok = 0;
    }
    else if(intersect== 0)
    {
        right_region = Region_create_intersect_right(region, cut_value, localdim);
        if (!right_region) ok = 0;
    }
    else if(intersect==+1)
    /* intersect is +1 if no overlap */
    {
        right_region = NULL;
    }

    if (ok)
    {
        if (!Node_is_leaf(left))
        {
            /* search for pairs in this half plane */
            ok = KDTree__neighbor_search(tree, left, left_region, depth+1);
        }
        else
        {
            ok = KDTree_search_neighbors_in_bucket(tree, left);
        }
    }

    if (ok)
    {
        if (!Node_is_leaf(right))
        {
            /* search for pairs in this half plane */
            ok = KDTree__neighbor_search(tree, right, right_region, depth+1);
        }
        else
        {
            ok = KDTree_search_neighbors_in_bucket(tree, right);
        }
    }

    /* search for pairs between the half planes */
    if (ok)
    {
        ok = KDTree_neighbor_search_pairs(tree, left, left_region, right, right_region, depth+1);
    }

    /* cleanup */
    Region_destroy(left_region);
    Region_destroy(right_region);

    return ok;
}

static int
KDTree_neighbor_search(struct KDTree* tree, float neighbor_radius,
                       struct Neighbor** neighbors)
{
    long int i;
    int ok;
    Region_dim = tree->dim;

    if(tree->_neighbor_list)
    {
        free(tree->_neighbor_list);
        tree->_neighbor_list = NULL;
    }
    tree->_neighbor_count= 0;
    /* note the use of r^2 to avoid use of sqrt */
    tree->_neighbor_radius=neighbor_radius;
    tree->_neighbor_radius_sq=neighbor_radius*neighbor_radius;

    if (Node_is_leaf(tree->_root))
    {
        /* this is a boundary condition */
        /* bucket_size>nr of points */
        ok = KDTree_search_neighbors_in_bucket(tree, tree->_root);
    }
    else
    {
        /* "normal" situation */
        struct Region *region;
        /* start with [-INF, INF] */
        region= Region_create(NULL, NULL);
        if (!region) return 0;
        ok = KDTree__neighbor_search(tree, tree->_root, region, 0);
        Region_destroy(region);
    }
    if (!ok) return 0;

    *neighbors = NULL;
    for (i = 0; i < tree->_neighbor_count; i++)
    {
        struct Neighbor* neighbor = malloc(sizeof(struct Neighbor));
        if (!neighbor)
        {
            while(1)
            {
                neighbor = *neighbors;
                if (!neighbor) return 0;
                *neighbors = neighbor->next;
                free(neighbor);
            }
        }
        *neighbor = tree->_neighbor_list[i];
        neighbor->next = *neighbors;
        *neighbors = neighbor;
    }

    return 1;
}

static int
KDTree_neighbor_simple_search(struct KDTree* tree, float radius,
                              struct Neighbor** neighbors)
{
    long int i;
    int ok = 1;

    Region_dim = tree->dim;

    tree->_neighbor_radius = radius;
    tree->_neighbor_radius_sq = radius*radius;

    tree->_neighbor_count= 0;
    if (tree->_neighbor_list)
    {
        free(tree->_neighbor_list);
        tree->_neighbor_list = NULL;
    }

    DataPoint_sort(tree->_data_point_list, tree->_data_point_list_size, 0);

    for (i = 0; i<tree->_data_point_list_size; i++)
    {
        float x1;
        long int j;
        struct DataPoint p1;

        p1 = tree->_data_point_list[i];
        x1=p1._coord[0];

        for (j=i+1; j<tree->_data_point_list_size; j++)
        {
            struct DataPoint p2;
            float x2;

            p2 = tree->_data_point_list[j];
            x2=p2._coord[0];

            if (fabs(x2-x1) <= radius)
            {
                ok = KDTree_test_neighbors(tree, &p1, &p2);
                if (!ok) break;
            }
            else
            {
                break;
            }
        }
    }

    if (!ok) return 0;

    *neighbors = NULL;

    for (i = 0; i < tree->_neighbor_count; i++)
    {
        struct Neighbor* neighbor = malloc(sizeof(struct Neighbor));
        if (!neighbor)
        {
            while(1)
            {
                neighbor = *neighbors;
                if (!neighbor) return 0;
                *neighbors = neighbor->next;
                free(neighbor);
            }
        }
        *neighbor = tree->_neighbor_list[i];
        neighbor->next = *neighbors;
        *neighbors = neighbor;
    }

    return 1;
}

static int KDTree_add_point(struct KDTree* tree, long int index, float *coord)
{
    int n;
    struct DataPoint* p;

    n = tree->_data_point_list_size;
    p = realloc(tree->_data_point_list, (n+1)*sizeof(struct DataPoint));
    if (p==NULL) return 0;

    p[n]._index = index;
    p[n]._coord = coord;

    tree->_data_point_list_size = n+1;
    tree->_data_point_list = p;

    return 1;
}

static struct Node *
KDTree_build_tree(struct KDTree* tree, long int offset_begin, long int offset_end, int depth)
{
    int localdim;

    if (depth== 0)
    {
        /* start with [begin, end+1] */
        offset_begin = 0;
        offset_end = tree->_data_point_list_size;
        localdim= 0;
    }
    else
    {
        localdim=depth%tree->dim;
    }

    if ((offset_end-offset_begin)<= tree->_bucket_size)
    {
        /* leaf node */
        return Node_create(-1, localdim, offset_begin, offset_end);
    }
    else
    {
        long int offset_split;
        long int left_offset_begin, left_offset_end;
        long int right_offset_begin, right_offset_end;
        long int d;
        float cut_value;
        struct DataPoint data_point;
        struct Node *left_node, *right_node, *new_node;

        DataPoint_sort(tree->_data_point_list+offset_begin, offset_end-offset_begin, localdim);

        /* calculate index of split point */
        d=offset_end-offset_begin;
        offset_split=d/2+d%2;

        data_point = tree->_data_point_list[offset_begin+offset_split-1];
        cut_value = data_point._coord[localdim];

        /* create new node and bind to left & right nodes */
        new_node=Node_create(cut_value, localdim, offset_begin, offset_end);
        if (new_node==NULL) return NULL;

        /* left */
        left_offset_begin=offset_begin;
        left_offset_end=offset_begin+offset_split;
        left_node=KDTree_build_tree(tree, left_offset_begin, left_offset_end, depth+1);

        /* right */
        right_offset_begin=left_offset_end;
        right_offset_end=offset_end;
        right_node=KDTree_build_tree(tree, right_offset_begin, right_offset_end, depth+1);

        new_node->_left = left_node;
        new_node->_right = right_node;

        if (left_node==NULL || right_node==NULL)
        {
            Node_destroy(new_node);
            return NULL;
        }

        return new_node;
    }
}

#define COPY2DARRAY(ctype) \
    for (i = 0; i < n; i++) { \
        for (j = 0; j < m; j++) { \
            coords[i*m+j] = *(ctype *) (p+i*rowstride+j*colstride); \
        } \
    }

static int KDTree_report_subtree(struct KDTree* tree, struct Node *node)
{
    int ok;
    if (Node_is_leaf(node))
    {
        /* report point(s) */
        long int i;

        for (i =node->_start; i<node->_end; i++)
        {
            struct DataPoint data_point;
            data_point = tree->_data_point_list[i];
            ok = KDTree_report_point(tree, data_point._index, data_point._coord);
            if (!ok) return 0;
        }
    }
    else
    {
        /* find points in subtrees via recursion */
        ok = KDTree_report_subtree(tree, node->_left);
        if (!ok) return 0;
        ok = KDTree_report_subtree(tree, node->_right);
        if (!ok) return 0;
    }
    return 1;
}

static int KDTree_search(struct KDTree* tree, struct Region *region, struct Node *node, int depth);

static int KDTree_test_region(struct KDTree* tree, struct Node *node, struct Region *region, int depth)
{
    int ok;
    int intersect_flag;

    /* is node region inside, outside or overlapping
     * with query region? */
    intersect_flag=Region_test_intersection(region, tree->_query_region, 0);

    if (intersect_flag==2)
    {
        /* inside - extract points */
        ok = KDTree_report_subtree(tree, node);
        /* end of recursion -- get rid of region */
        Region_destroy(region);
        if (!ok) return 0;
    }
    else if (intersect_flag==1)
    {
        /* overlap - recursion */
        ok = KDTree_search(tree, region, node, depth+1);
        /* search does cleanup of region */
        if (!ok) return 0;
    }
    else
    {
        /* outside - stop */

        /* end of recursion -- get rid of region */
        Region_destroy(region);
    }
    return 1;
}

static int KDTree_search(struct KDTree* tree, struct Region *region, struct Node *node, int depth)
{
    int current_dim;
    int ok = 1;

    if(depth== 0)
    {
        /* start with [-INF, INF] region */

        region = Region_create(NULL, NULL);
        if (region==NULL) return 0;

        /* start with root node */
        node = tree->_root;
    }

    current_dim=depth%tree->dim;

    if(Node_is_leaf(node))
    {
        long int i;

        for (i =node->_start; i<node->_end; i++)
        {
            struct DataPoint data_point;

            data_point = tree->_data_point_list[i];

            if (Region_encloses(tree->_query_region, data_point._coord))
            {
                /* point is enclosed in query region - report & stop */
                ok = KDTree_report_point(tree, data_point._index, data_point._coord);
            }
        }
    }
    else
    {
        struct Node *left_node, *right_node;
        struct Region *left_region, *right_region;
        int intersect_left, intersect_right;

        left_node=node->_left;

        /* LEFT HALF PLANE */

        /* new region */
        intersect_left=Region_test_intersect_left(region, node->_cut_value, current_dim);

        if(intersect_left==1)
        {
            left_region = Region_create(region->_left, region->_right);
            if (left_region)
                ok = KDTree_test_region(tree, left_node, left_region, depth);
            else
                ok = 0;
        }
        else if (intersect_left== 0)
        {
            left_region = Region_create_intersect_left(region, node->_cut_value, current_dim);
            if (left_region)
                ok = KDTree_test_region(tree, left_node, left_region, depth);
            else
                ok = 0;
        }
        /* intersect_left is -1 if no overlap */

        /* RIGHT HALF PLANE */

        right_node=node->_right;

        /* new region */
        intersect_right=Region_test_intersect_right(region, node->_cut_value, current_dim);
        if (intersect_right==-1)
        {
            right_region = Region_create(region->_left, region->_right);
            /* test for overlap/inside/outside & do recursion/report/stop */
            if (right_region)
                ok = KDTree_test_region(tree, right_node, right_region, depth);
            else
                ok = 0;
        }
        else if (intersect_right== 0)
        {
            right_region = Region_create_intersect_right(region, node->_cut_value, current_dim);
            /* test for overlap/inside/outside & do recursion/report/stop */
            if (right_region)
                ok = KDTree_test_region(tree, right_node, right_region, depth);
            else
                ok = 0;
        }
        /* intersect_right is +1 if no overlap */
    }

    Region_destroy(region);
    return ok;
}

/* Python interface */

typedef struct {
    PyObject_HEAD
    struct Neighbor neighbor;
} PyNeighbor;

static int
PyNeighbor_init(PyNeighbor *self, PyObject *args, PyObject *kwds)
{
    long int index1, index2;
    float radius = 0.0;
    static char *kwlist[] = {"index1", "index2", "radius", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "ii|d", kwlist,
                                      &index1, &index2, &radius))
        return -1;
    self->neighbor.index1 = index1;
    self->neighbor.index2 = index2;
    self->neighbor.radius = radius;

    return 0;
}

static PyObject*
PyNeighbor_repr(PyNeighbor* self)
{
    char string[64];
    sprintf(string, "(%ld, %ld): %g",
            self->neighbor.index1, self->neighbor.index2, self->neighbor.radius);
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_FromFormat(string);
#else
    return PyString_FromString(string);
#endif

}

static char PyNeighbor_index1__doc__[] =
"index of the first neighbor";

static PyObject*
PyNeighbor_getindex1(PyNeighbor* self, void* closure)
{
#if PY_MAJOR_VERSION >= 3
    return PyLong_FromLong(self->neighbor.index1);
#else
    return PyInt_FromLong(self->neighbor.index1);
#endif
}

static int
PyNeighbor_setindex1(PyNeighbor* self, PyObject* value, void* closure)
{
#if PY_MAJOR_VERSION >= 3
    long index1 = PyLong_AsLong(value);
#else
    long index1 = PyInt_AsLong(value);
#endif
    if (PyErr_Occurred()) return -1;
    self->neighbor.index1 = index1;
    return 0;
}

static char PyNeighbor_index2__doc__[] =
"index of the second neighbor";

static PyObject*
PyNeighbor_getindex2(PyNeighbor* self, void* closure)
{
#if PY_MAJOR_VERSION >= 3
    return PyLong_FromLong(self->neighbor.index2);
#else
    return PyInt_FromLong(self->neighbor.index2);
#endif
}

static int
PyNeighbor_setindex2(PyNeighbor* self, PyObject* value, void* closure)
{
#if PY_MAJOR_VERSION >= 3
    long index2 = PyLong_AsLong(value);
#else
    long index2 = PyInt_AsLong(value);
#endif
    if (PyErr_Occurred()) return -1;
    self->neighbor.index2 = index2;
    return 0;
}

static PyObject*
PyNeighbor_getradius(PyNeighbor* self, void* closure)
{
    const float value = self->neighbor.radius;
    return PyFloat_FromDouble((double)value);
}

static int
PyNeighbor_setradius(PyNeighbor* self, PyObject* value, void* closure)
{
    const double radius = PyFloat_AsDouble(value);
    if (PyErr_Occurred()) return -1;
    self->neighbor.radius = (float)radius;
    return 0;
}

static char PyNeighbor_radius__doc__[] =
"the radius\n";

static PyGetSetDef PyNeighbor_getset[] = {
    {"index1", (getter)PyNeighbor_getindex1, (setter)PyNeighbor_setindex1, PyNeighbor_index1__doc__, NULL},
    {"index2", (getter)PyNeighbor_getindex2, (setter)PyNeighbor_setindex2, PyNeighbor_index2__doc__, NULL},
    {"radius", (getter)PyNeighbor_getradius, (setter)PyNeighbor_setradius, PyNeighbor_radius__doc__, NULL},
    {NULL}  /* Sentinel */
};

static char PyNeighbor_doc[] =
"A neighbor pair; members are index1, index2, and radius.\n";

static PyTypeObject PyNeighborType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "KDTree.Neighbor",         /* tp_name*/
    sizeof(PyNeighbor),        /* tp_basicsize*/
    0,                         /* tp_itemsize*/
    0,                         /* tp_dealloc*/
    0,                         /* tp_print*/
    0,                         /* tp_getattr*/
    0,                         /* tp_setattr*/
    0,                         /* tp_compare*/
    (reprfunc)PyNeighbor_repr, /* tp_repr*/
    0,                         /* tp_as_number*/
    0,                         /* tp_as_sequence*/
    0,                         /* tp_as_mapping*/
    0,                         /* tp_hash */
    0,                         /* tp_call*/
    0,                         /* tp_str*/
    0,                         /* tp_getattro*/
    0,                         /* tp_setattro*/
    0,                         /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /* tp_flags*/
    PyNeighbor_doc,            /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    PyNeighbor_getset,         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)PyNeighbor_init, /* tp_init */
};

typedef struct {
    PyObject_HEAD
    struct KDTree tree;
} PyTree;

static void
PyTree_dealloc(PyTree* self)
{
    struct KDTree* tree = &self->tree;
    if (!tree->_root) return;
    Node_destroy(tree->_root);
    Region_destroy(tree->_query_region);
    if (tree->_center_coord) free(tree->_center_coord);
    if (tree->_coords) free(tree->_coords);
    if (tree->_data_point_list) free(tree->_data_point_list);
    if (tree->_neighbor_list) free(tree->_neighbor_list);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int
PyTree_init(PyTree* self, PyObject* args, PyObject* kwds)
{
    int dim;
    int bucket_size;
    struct KDTree* tree;

    if(!PyArg_ParseTuple(args, "ii:KDTree_init" ,&dim, &bucket_size)) return -1;

    if (dim <= 0 || bucket_size <= 0) {
        PyErr_SetString(PyExc_ValueError, "Both arguments should be positive");
        return -1;
    }

    tree = &self->tree;

    tree->dim = dim;
    tree->_query_region = NULL;
    tree->_root = NULL;
    tree->_coords = NULL;
    tree->_radius_list = NULL;
    tree->_count = 0;
    tree->_neighbor_count = 0;
    tree->_neighbor_list = NULL;
    tree->_bucket_size = bucket_size;
    tree->_data_point_list = NULL;
    tree->_data_point_list_size = 0;

    tree->_center_coord = malloc(dim*sizeof(float));
    if (tree->_center_coord == NULL)
    {
        free(tree);
        PyErr_SetString(PyExc_MemoryError, "Insufficient memory for tree");
        return -1;
    }

    Region_dim = tree->dim;

    return 0;
}

static PyObject*
PyTree_get_count(PyTree* self)
{
    long count;
    PyObject* result;
    count = self->tree._count;
#if PY_MAJOR_VERSION >= 3
    result = PyLong_FromLong(count);
#else
    result = PyInt_FromLong(count);
#endif
    if (!result) {
        PyErr_SetString (PyExc_MemoryError, "Failed to allocate memory for object.");
        return NULL;
    }
    return result;
}

static PyObject*
PyTree_neighbor_get_count(PyTree* self)
{
    PyObject* result;
    long count = self->tree._neighbor_count;
#if PY_MAJOR_VERSION >= 3
    result = PyLong_FromLong(count);
#else
    result = PyInt_FromLong(count);
#endif
    if (!result)
    {
        PyErr_SetString (PyExc_MemoryError, "Failed to allocate memory for object.");
        return NULL;
    }
    return result;
}


static PyObject*
PyTree_set_data(PyTree* self, PyObject* args)
{
    float* coords;
    Py_ssize_t n, m, i, j;
    PyObject *obj;
    struct KDTree* tree = &self->tree;
    int ok;
    Py_ssize_t rowstride, colstride;
    const char* p;
    const int flags = PyBUF_FORMAT | PyBUF_STRIDES;
    char datatype;
    Py_buffer view;

    if(!PyArg_ParseTuple(args, "O:KDTree_set_data",&obj)) return NULL;

    if (PyObject_GetBuffer(obj, &view, flags) == -1) return NULL;
    if (view.ndim != 2) {
        PyErr_SetString(PyExc_RuntimeError, "Array must be two-dimensional");
        return NULL;
    }
    n = view.shape[0];
    m = view.shape[1];
    rowstride = view.strides[0];
    colstride = view.strides[1];
    /* coord_data is deleted by the KDTree object */
    coords = malloc(m*n*sizeof(float));
    if (!coords) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate memory for coordinates.");
        goto exit;
    }
    p = view.buf;
    datatype = view.format[0];
    switch (datatype) {
        case '@':
        case '=':
        case '<':
        case '>':
        case '!': datatype = view.format[1]; break;
        default: break;
    }
    switch (datatype) {
        case 'd': COPY2DARRAY(double); break;
        case 'f': COPY2DARRAY(float); break;
        case 'i': COPY2DARRAY(int); break;
        case 'I': COPY2DARRAY(unsigned int); break;
        case 'l': COPY2DARRAY(long); break;
        case 'L': COPY2DARRAY(unsigned long); break;
        default:
            PyErr_Format(PyExc_RuntimeError,
                "array should contain numerical data (format character was %c).",
                datatype);
            goto exit;
    }

    Region_dim = tree->dim;

    /* clean up stuff from previous use */
    Node_destroy(tree->_root);
    if (tree->_coords) free(tree->_coords);
    if (tree->_radius_list)
    {
        free(tree->_radius_list);
        tree->_radius_list = NULL;
    }
    tree->_count = 0;
    /* keep pointer to coords to delete it */
    tree->_coords = coords;

    for (i = 0; i<n; i++)
    {
        ok = KDTree_add_point(tree, i, coords+i*tree->dim);
        if (!ok) 
        {
            free(tree->_data_point_list);
            tree->_data_point_list = NULL;
            tree->_data_point_list_size = 0;
            goto exit;
        }
    }

    /* build KD tree */
    tree->_root = KDTree_build_tree(tree, 0, 0, 0);
    if(!tree->_root) {
        PyErr_SetString (PyExc_MemoryError, "Failed to allocate memory for nodes.");
        goto exit;
    }
    PyBuffer_Release(&view);
    Py_INCREF(Py_None);
    return Py_None;

exit:
    PyBuffer_Release(&view);
    if (coords) free(coords);
    return NULL;
}

#define COPY1DARRAY(ctype) \
    for (i = 0; i < n; i++) { \
        coords[i] = *(ctype *) (p+i*stride); \
    }

static PyObject*
PyTree_search_center_radius(PyTree* self, PyObject* args)
{
    PyObject *obj;
    double radius;
    long int n, i;
    float *coords;
    struct KDTree* tree = &self->tree;
    const int flags = PyBUF_FORMAT | PyBUF_STRIDES;
    Py_ssize_t stride;
    Py_buffer view;
    char datatype;
    const char* p;
    int dim = tree->dim;
    float* left;
    float* right;

    if(!PyArg_ParseTuple(args, "Od:KDTree_search_center_radius", &obj ,&radius))
        return NULL;

    if(radius <= 0)
    {
        PyErr_SetString(PyExc_ValueError, "Radius must be positive.");
        return NULL;
    }

    if (PyObject_GetBuffer(obj, &view, flags) == -1) return NULL;
    if (view.ndim != 1) {
        PyErr_SetString(PyExc_RuntimeError, "Array must be one-dimensional");
        return NULL;
    }
    n = view.shape[0];
    stride = view.strides[0];
    /* coord_data is deleted by the KDTree object */
    coords = malloc(n*sizeof(float));
    if (!coords) {
        PyErr_NoMemory();
        goto exit;
    }
    p = view.buf;
    datatype = view.format[0];
    switch (datatype) {
        case '@':
        case '=':
        case '<':
        case '>':
        case '!': datatype = view.format[1]; break;
        default: break;
    }
    switch (datatype) {
        case 'd': COPY1DARRAY(double); break;
        case 'f': COPY1DARRAY(float); break;
        case 'i': COPY1DARRAY(int); break;
        case 'I': COPY1DARRAY(unsigned int); break;
        case 'l': COPY1DARRAY(long); break;
        case 'L': COPY1DARRAY(unsigned long); break;
        default:
            PyErr_Format(PyExc_RuntimeError,
                "array should contain numerical data (format character was %c.",
                datatype);
            goto exit;
    }

    left = malloc(dim*sizeof(float));
    right = malloc(dim*sizeof(float));
    if (left==NULL || right==NULL)
    {
        if (left) free(left);
        if (right) free(right);
        PyErr_NoMemory();
        goto exit;
    }

    Region_dim = tree->dim;

    if (tree->_radius_list)
    {
        free(tree->_radius_list);
        tree->_radius_list = NULL;
    }
    tree->_count = 0;

    tree->_radius = radius;
    /* use of r^2 to avoid sqrt use */
    tree->_radius_sq = radius*radius;

    for (i = 0; i<tree->dim; i++)
    {
        left[i] = coords[i]-radius;
        right[i] = coords[i]+radius;
        /* set center of query */
        tree->_center_coord[i] = coords[i];
    }

    /* clean up! */
    if (coords) free(coords);

    Region_destroy(tree->_query_region);
    tree->_query_region = Region_create(left, right);

    free(left);
    free(right);

    if (!tree->_query_region) {
        PyErr_NoMemory();
        goto exit;
    }

    if (!KDTree_search(tree, NULL, NULL, 0)) {
        PyErr_NoMemory();
        goto exit;
    }

    PyBuffer_Release(&view);
    Py_INCREF(Py_None);
    return Py_None;

exit:
    PyBuffer_Release(&view);
    if (coords) free(coords);
    return NULL;
}

static PyObject*
PyTree_neighbor_search(PyTree* self, PyObject* args)
{
    int ok;
    double radius;
    struct KDTree* tree = &self->tree;
    struct Neighbor* neighbors;
    struct Neighbor* pp, *qq;
    PyObject* list;
    Py_ssize_t i, n;

    if(!PyArg_ParseTuple(args, "d:KDTree_neighbor_search", &radius))
        return NULL;

    if(radius <= 0)
    {
        PyErr_SetString(PyExc_ValueError, "Radius must be positive.");
        return NULL;
    }

    ok = KDTree_neighbor_search(tree, radius, &neighbors);
    if (!ok)
    {
        PyErr_SetString(PyExc_MemoryError,
            "calculation failed due to lack of memory");
        return NULL;
    }

    pp = neighbors;
    n = 0;
    while (pp)
    {
        n+=1;
        pp = pp->next;
    }

    list = PyList_New(n);
    if (list)
    {
        PyNeighbor* p;
        pp = neighbors;
        for (i = 0; i < n; i++)
        {
            p = (PyNeighbor*) PyNeighborType.tp_alloc(&PyNeighborType, 0);
            if(!p)
            {
                PyErr_SetString(PyExc_MemoryError,
                    "could not create node for return value");
                Py_DECREF(list);
                return NULL;
            }
            p->neighbor = *pp;
            PyList_SET_ITEM(list, i, (PyObject*)p);
            qq = pp->next;
            free(pp);
            pp = qq;
        }
    }

    return list;
}

static PyObject*
PyTree_neighbor_simple_search(PyTree* self, PyObject* args)
{
    int ok;
    double radius;
    struct KDTree* tree = &self->tree;
    struct Neighbor* neighbors;
    struct Neighbor* pp, *qq;
    PyObject* list;
    Py_ssize_t i, n;

    if(!PyArg_ParseTuple(args, "d:KDTree_neighbor_simple_search", &radius))
        return NULL;

    if(radius <= 0)
    {
        PyErr_SetString(PyExc_ValueError, "Radius must be positive.");
        return NULL;
    }

    ok = KDTree_neighbor_simple_search(tree, radius, &neighbors);
    if (!ok)
    {
        PyErr_SetString(PyExc_MemoryError,
            "calculation failed due to lack of memory");
        return NULL;
    }

    pp = neighbors;
    n = 0;
    while (pp)
    {
        n+=1;
        pp = pp->next;
    }

    list = PyList_New(n);
    if (list)
    {
        PyNeighbor* p;
        pp = neighbors;
        for (i = 0; i < n; i++)
        {
            p = (PyNeighbor*) PyNeighborType.tp_alloc(&PyNeighborType, 0);
            if(!p)
            {
                PyErr_SetString(PyExc_MemoryError,
                    "could not create node for return value");
                Py_DECREF(list);
                return NULL;
            }
            p->neighbor = *pp;
            PyList_SET_ITEM(list, i, (PyObject*)p);
            qq = pp->next;
            free(pp);
            pp = qq;
        }
    }

    return list;
}

static char PyTree_get_indices__doc__[] =
"returns indices of coordinates within radius as a Numpy array\n";

static PyObject *PyTree_get_indices(PyTree *self, PyObject* args)
{
    struct KDTree* tree = &self->tree;
    const int flags = PyBUF_C_CONTIGUOUS | PyBUF_FORMAT;
    char datatype;
    Py_buffer view;
    PyObject* object;
    Py_ssize_t i;
    Py_ssize_t* indices;

    if (!PyArg_ParseTuple(args, "O:KDTree_get_indices", &object)) return NULL;
    if (PyObject_GetBuffer(object, &view, flags) == -1)
        return NULL;
    datatype = view.format[0];
    switch (datatype) {
        case '@':
        case '=':
        case '<':
        case '>':
        case '!': datatype = view.format[1]; break;
        default: break;
    }
    if (datatype != 'l') {
        PyErr_Format(PyExc_RuntimeError,
            "array has incorrect data format ('%c', expected 'l')", datatype);
        PyBuffer_Release(&view);
        return NULL;
    }
    else if (view.ndim != 1) {
        PyErr_Format(PyExc_ValueError,
            "array has incorrect rank (%d expected 1)", view.ndim);
        PyBuffer_Release(&view);
        return NULL;
    }
    /* copy the data into the Numpy data pointer */
    indices = view.buf;
    for (i = 0; i<tree->_count; i++) indices[i] = tree->_radius_list[i].index;

    PyBuffer_Release(&view);
    Py_INCREF(Py_None);
    return Py_None;
}

static char PyTree_get_radii__doc__[] =
"returns distances of coordinates within radius as a Numpy array.\n";

static PyObject *PyTree_get_radii(PyTree *self, PyObject* args)
{
    PyObject* object;
    const int flags = PyBUF_C_CONTIGUOUS | PyBUF_FORMAT;
    char datatype;
    Py_buffer view;
    struct KDTree* tree = &self->tree;
    float *radii;
    Py_ssize_t i;

    if (!PyArg_ParseTuple(args, "O:KDTree_get_radii", &object)) return NULL;
    if (PyObject_GetBuffer(object, &view, flags) == -1)
        return NULL;
    datatype = view.format[0];
    switch (datatype) {
        case '@':
        case '=':
        case '<':
        case '>':
        case '!': datatype = view.format[1]; break;
        default: break;
    }
    if (datatype != 'l') {
        PyErr_Format(PyExc_RuntimeError,
            "array has incorrect data format ('%c', expected 'f')", datatype);
        PyBuffer_Release(&view);
        return NULL;
    }
    else if (view.ndim != 1) {
        PyErr_Format(PyExc_ValueError,
            "array has incorrect rank (%d expected 1)", view.ndim);
        PyBuffer_Release(&view);
        return NULL;
    }

    /* copy the data into the Numpy data pointer */
    radii = view.buf;
    for (i = 0; i<tree->_count; i++) radii[i] = tree->_radius_list[i].value;

    PyBuffer_Release(&view);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef PyTree_methods[] = {
    {"get_count", (PyCFunction)PyTree_get_count, METH_NOARGS, NULL},
    {"set_data", (PyCFunction)PyTree_set_data, METH_VARARGS, NULL},
    {"search_center_radius", (PyCFunction)PyTree_search_center_radius, METH_VARARGS, NULL},
    {"neighbor_get_count", (PyCFunction)PyTree_neighbor_get_count, METH_NOARGS, NULL},
    {"neighbor_search", (PyCFunction)PyTree_neighbor_search, METH_VARARGS, NULL},
    {"neighbor_simple_search", (PyCFunction)PyTree_neighbor_simple_search, METH_VARARGS, NULL},
    {"get_indices", (PyCFunction)PyTree_get_indices, METH_VARARGS, PyTree_get_indices__doc__},
    {"get_radii", (PyCFunction)PyTree_get_radii, METH_VARARGS, PyTree_get_radii__doc__},
    {NULL}  /* Sentinel */
};

static char PyTree_doc[] = "C KDTree.\n";

static PyTypeObject PyTreeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "C KDTree",                  /*tp_name*/
    sizeof(PyTree),              /*tp_basicsize*/
    0,                           /*tp_itemsize*/
    (destructor)PyTree_dealloc,  /*tp_dealloc*/
    0,                           /*tp_print*/
    0,                           /*tp_getattr*/
    0,                           /*tp_setattr*/
    0,                           /*tp_compare*/
    0,                           /*tp_repr*/
    0,                           /*tp_as_number*/
    0,                           /*tp_as_sequence*/
    0,                           /*tp_as_mapping*/
    0,                           /*tp_hash */
    0,                           /*tp_call*/
    0,                           /*tp_str*/
    0,                           /*tp_getattro*/
    0,                           /*tp_setattro*/
    0,                           /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,          /*tp_flags*/
    PyTree_doc,                  /* tp_doc */
    0,                           /* tp_traverse */
    0,                           /* tp_clear */
    0,                           /* tp_richcompare */
    0,                           /* tp_weaklistoffset */
    0,                           /* tp_iter */
    0,                           /* tp_iternext */
    PyTree_methods,              /* tp_methods */
    NULL,                        /* tp_members */
    0,                           /* tp_getset */
    0,                           /* tp_base */
    0,                           /* tp_dict */
    0,                           /* tp_descr_get */
    0,                           /* tp_descr_set */
    0,                           /* tp_dictoffset */
    (initproc)PyTree_init,       /* tp_init */
};

/* ========================================================================== */
/* -- Initialization -------------------------------------------------------- */
/* ========================================================================== */

#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_kdtrees",
        NULL,
        -1,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};

PyObject *
PyInit__kdtrees(void)

#else

void
init_kdtrees(void)
#endif
{
  PyObject *module;

  PyTreeType.tp_new = PyType_GenericNew;
  PyNeighborType.tp_new = PyType_GenericNew;
  if (PyType_Ready(&PyTreeType) < 0)
#if PY_MAJOR_VERSION >= 3
      return NULL;
#else
      return;
#endif
  if (PyType_Ready(&PyNeighborType) < 0)
#if PY_MAJOR_VERSION >= 3
      return NULL;
#else
      return;
#endif

#if PY_MAJOR_VERSION >= 3
  module = PyModule_Create(&moduledef);
  if (module == NULL) return NULL;
#else
  module = Py_InitModule("_kdtrees", NULL);
  if (module == NULL) return;
#endif

  Py_INCREF(&PyTreeType);
  Py_INCREF(&PyNeighborType);
  PyModule_AddObject(module, "KDTree", (PyObject*) &PyTreeType);
  PyModule_AddObject(module, "Neighbor", (PyObject*) &PyNeighborType);

  if (PyErr_Occurred()) Py_FatalError("can't initialize module _kdtrees");
#if PY_MAJOR_VERSION >= 3
  return module;
#endif
}
