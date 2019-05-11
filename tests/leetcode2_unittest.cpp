#include <stdio.h>
#include <vector>
#include <stack>

using namespace std;

struct Tree
{
    struct Tree *lch;
    struct Tree *rch;
    int val;
};

Tree* findTopK1(Tree *root, int &top_k)
{
    if (root == NULL)
    {
        return NULL;
    }

    Tree* t = findTopK1(root->lch, top_k);
    top_k--;
    if (top_k <= 0)
    {
        return root;
    }
    if (t != NULL)
    {
        return t;
    }
    else 
    {
        return findTopK1(root->rch, top_k);
    }
}

// int findTopK(Tree *root, int top_k)
// {
//     if (root == NULL)
//     {
//         return -1;
//     }

//     stack<Tree*> s;
//     s.push(root);

//     while (!s.empty())
//     {
//         Tree* p = s.top();
//         s.pop();

//         if (p->lch != NULL)
//         {
//             s.push(p->lch);
//         }
//         if (p->rch != NULL)
//         {
//             s.push(p->rch);
//         }
//     }

//     int count = s.size() - top_k;
//     while (count > 0 && !s.empty())
//     {
//         s.pop();
//     }
    
//     if (!s.empty())
//     {
//         return s.top()->val;
//     }

//     return -1;
// }

int findTopK2(vector<int> &arr1, vector<int> &arr2, int top_k)
{
    int arr1_idx = 0, arr2_idx = 0;
    while (arr1_idx < arr1.size() && arr2_idx < arr2.size())
    {
        if (arr1[arr1_idx] < arr2[arr2_idx])
        {
            top_k--;
            if (top_k <= 0)
            {
                return arr1[arr1_idx];
            }
            arr1_idx++;
        }
        else if (arr1[arr1_idx] == arr2[arr2_idx])
        {
            arr1_idx++;
            arr2_idx++;
        }
        else
        {
            top_k--;
            if (top_k <= 0)
            {
                return arr2[arr2_idx];
            }
            arr2_idx++;
        }
    }

    while (arr1_idx < arr1.size())
    {
        top_k--;
        if (top_k <= 0)
        {
            return arr1[arr1_idx];
        }
        arr1_idx++;
    }

    while (arr2_idx < arr2.size())
    {
        top_k--;
        if (top_k <= 0)
        {
            return arr2[arr2_idx];
        }
        arr2_idx++;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    vector<int> a1, a2;
    a1.push_back(1);
    a1.push_back(3);
    a1.push_back(8);

    a2.push_back(2);
    a2.push_back(5);
    int ret = findTopK2(a1, a2, 3);  

    fprintf(stdout, "ret : %d", ret);
}