//----------------------------------------------------------------------------
// Post-order depth-first traversal algorithm used to clear an LLRB tree
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Alexis J. Malozemoff <alexis.malozemoff@pnnl.gov>
//   Peter L. Nordquist <peter.nordquist@pnnl.gov>
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//   Ruslan A. Doroshchuk <ruslan.doroshchuk@pnnl.gov>
//----------------------------------------------------------------------------

#include "llrb.h"

//----------------------------------------------------------------------------
/// @brief Generates the function to clear the tree
///
/// The del function takes a pointer to the node to delete.
///
/// Based on LLRB_GENERATE.
///
/// @param name   Unique prefix of the LLRB tree to operate on
/// @param type   Type of user-defined structure the makes up the tree nodes
/// @param field  Field in the structure that contains the tree pointers
/// @param del    Function to call to delete each node
#define LLRB_CLEAR_GENERATE(name, type, field, del) \
	static inline void name##_LLRB_CLEAR(struct name *head) { \
		struct type *elm = head->rbh_root; \
		while (elm) { \
			if (elm->field.rbe_left) { \
				elm = elm->field.rbe_left; \
			} else if (elm->field.rbe_right) { \
				elm = elm->field.rbe_right; \
			} else { \
				struct type *tmp = elm; \
				elm = elm->field.rbe_parent; \
				if (elm) { \
					if (elm->field.rbe_left == tmp) { \
						elm->field.rbe_left = NULL; \
					} else if (elm->field.rbe_right == tmp) { \
						elm->field.rbe_right = NULL; \
					} \
				} \
				(del)(tmp); \
			} \
		} \
		head->rbh_root = NULL; \
	}

//----------------------------------------------------------------------------
/// @brief Deletes every node in the tree
///
/// @param name   Unique prefix of the LLRB tree to operate on
/// @param head   Pointer to the head of the tree
#define LLRB_CLEAR(name, head) name##_LLRB_CLEAR((head))
