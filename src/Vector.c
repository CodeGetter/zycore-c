/***************************************************************************************************

  Zyan Core Library (Zycore-C)

  Original Author : Florian Bernd

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.

***************************************************************************************************/

#include <Zycore/LibC.h>
#include <Zycore/Vector.h>

/* ============================================================================================== */
/* Internal macros                                                                                */
/* ============================================================================================== */

/**
 * @brief   Checks, if the passed vector should grow.
 *
 * @param   size        The desired size of the vector.
 * @param   capacity    The current capacity of the vector.
 *
 * @return  `ZYAN_TRUE`, if the vector should grow or `ZYAN_FALSE`, if not.
 */
#define ZYCORE_VECTOR_SHOULD_GROW(size, capacity) \
    ((size) > (capacity))

/**
 * @brief   Checks, if the passed vector should shrink.
 *
 * @param   size        The desired size of the vector.
 * @param   capacity    The current capacity of the vector.
 * @param   threshold   The shrink threshold.
 *
 * @return  `ZYAN_TRUE`, if the vector should shrink or `ZYAN_FALSE`, if not.
 */
#define ZYCORE_VECTOR_SHOULD_SHRINK(size, capacity, threshold) \
    ((size) < (capacity) * (threshold))

/**
 * @brief   Returns the offset of the element at the given `index`.
 *
 * @param   vector  A pointer to the `ZyanVector` instance.
 * @param   index   The element index.
 *
 * @return  The offset of the element at the given `index`.
 */
#define ZYCORE_VECTOR_OFFSET(vector, index) \
    ((void*)((ZyanU8*)(vector)->data + ((index) * (vector)->element_size)))

/* ============================================================================================== */
/* Internal functions                                                                             */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Helper functions                                                                               */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Reallocates the internal buffer of the vector.
 *
 * @param   vector      A pointer to the `ZyanVector` instance.
 * @param   capacity    The new capacity.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyanVectorReallocate(ZyanVector* vector, ZyanUSize capacity)
{
    ZYAN_ASSERT(vector);
    ZYAN_ASSERT(vector->capacity >= ZYAN_VECTOR_MIN_CAPACITY);
    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    if (!vector->allocator)
    {
        if (vector->capacity < capacity)
        {
            return ZYAN_STATUS_INSUFFICIENT_BUFFER_SIZE;
        }
        return ZYAN_STATUS_SUCCESS;
    }

    ZYAN_ASSERT(vector->allocator);
    ZYAN_ASSERT(vector->allocator->reallocate);

    if (capacity < ZYAN_VECTOR_MIN_CAPACITY)
    {
        if (vector->capacity > ZYAN_VECTOR_MIN_CAPACITY)
        {
            capacity = ZYAN_VECTOR_MIN_CAPACITY;
        } else
        {
            return ZYAN_STATUS_SUCCESS;
        }
    }

    vector->capacity = capacity;
    ZYAN_CHECK(vector->allocator->reallocate(vector->allocator, &vector->data,
        vector->element_size, vector->capacity));

    return ZYAN_STATUS_SUCCESS;
}

/**
 * @brief   Shifts all elements starting at the specified `index` by the amount of `count` to the
 *          left.
 *
 * @param   vector  A pointer to the `ZyanVector` instance.
 * @param   index   The start index.
 * @param   count   The amount of shift operations.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyanVectorShiftLeft(ZyanVector* vector, ZyanUSize index, ZyanUSize count)
{
    ZYAN_ASSERT(vector);
    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);
    ZYAN_ASSERT(index >= 0);
    ZYAN_ASSERT(count >  0);
    //ZYAN_ASSERT((ZyanISize)count - (ZyanISize)index + 1 >= 0);

    void* const source   = ZYCORE_VECTOR_OFFSET(vector, index + count);
    void* const dest     = ZYCORE_VECTOR_OFFSET(vector, index);
    const ZyanUSize size = (vector->size - index) * vector->element_size;
    ZYAN_MEMMOVE(dest, source, size);

    return ZYAN_STATUS_SUCCESS;
}

/**
 * @brief   Shifts all elements starting at the specified `index` by the amount of `count` to the
 *          right.
 *
 * @param   vector  A pointer to the `ZyanVector` instance.
 * @param   index   The start index.
 * @param   count   The amount of shift operations.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyanVectorShiftRight(ZyanVector* vector, ZyanUSize index, ZyanUSize count)
{
    ZYAN_ASSERT(vector);
    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);
    ZYAN_ASSERT(count > 0);
    ZYAN_ASSERT(vector->size + count <= vector->capacity);

    void* const source   = ZYCORE_VECTOR_OFFSET(vector, index);
    void* const dest     = ZYCORE_VECTOR_OFFSET(vector, index + count);
    const ZyanUSize size = (vector->size - index) * vector->element_size;
    ZYAN_MEMMOVE(dest, source, size);

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
/* Exported functions                                                                             */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Constructor and destructor                                                                     */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorInit(ZyanVector* vector, ZyanUSize element_size, ZyanUSize capacity)
{
    return ZyanVectorInitEx(vector,  element_size, capacity, ZyanAllocatorDefault(),
        ZYAN_VECTOR_DEFAULT_GROWTH_FACTOR, ZYAN_VECTOR_DEFAULT_SHRINK_THRESHOLD);
}

ZyanStatus ZyanVectorInitEx(ZyanVector* vector, ZyanUSize element_size, ZyanUSize capacity,
    ZyanAllocator* allocator, float growth_factor, float shrink_threshold)
{
    if (!vector || !element_size || !allocator || (growth_factor < 1.0f) ||
        (shrink_threshold < 0.0f) || (shrink_threshold > 1.0f))
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    ZYAN_ASSERT(allocator->allocate);

    vector->allocator        = allocator;
    vector->growth_factor    = growth_factor;
    vector->shrink_threshold = shrink_threshold;
    vector->size             = 0;
    vector->capacity         = ZYAN_MAX(ZYAN_VECTOR_MIN_CAPACITY, capacity);
    vector->element_size     = element_size;
    vector->data             = ZYAN_NULL;

    return allocator->allocate(vector->allocator, &vector->data, vector->element_size,
        vector->capacity);
}

ZyanStatus ZyanVectorInitCustomBuffer(ZyanVector* vector, ZyanUSize element_size,
    void* buffer, ZyanUSize capacity)
{
    if (!vector || !element_size || !buffer || !capacity)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    vector->allocator        = ZYAN_NULL;
    vector->growth_factor    = 1.0f;
    vector->shrink_threshold = 0.0f;
    vector->size             = 0;
    vector->capacity         = capacity;
    vector->element_size     = element_size;
    vector->data             = buffer;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorDestroy(ZyanVector* vector)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    if (vector->allocator && vector->capacity)
    {
        ZYAN_ASSERT(vector->allocator->deallocate);
        ZYAN_CHECK(vector->allocator->deallocate(vector->allocator, vector->data,
            vector->element_size, vector->capacity));
    }

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */
/* Duplication                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorDuplicate(ZyanVector* destination, const ZyanVector* source,
    ZyanUSize capacity)
{
    return ZyanVectorDuplicateEx(destination, source, capacity, ZyanAllocatorDefault(),
        ZYAN_VECTOR_DEFAULT_GROWTH_FACTOR, ZYAN_VECTOR_DEFAULT_SHRINK_THRESHOLD);
}

ZyanStatus ZyanVectorDuplicateEx(ZyanVector* destination, const ZyanVector* source,
    ZyanUSize capacity, ZyanAllocator* allocator, float growth_factor, float shrink_threshold)
{
    if (!source)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    const ZyanUSize len = source->size;

    capacity = ZYAN_MAX(capacity, len);
    ZYAN_CHECK(ZyanVectorInitEx(destination, source->element_size, capacity, allocator,
        growth_factor, shrink_threshold));
    ZYAN_ASSERT(destination->capacity >= len);

    ZYAN_MEMCPY(destination->data, source->data, len * source->element_size);
    destination->size = len;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorDuplicateCustomBuffer(ZyanVector* destination, const ZyanVector* source,
    void* buffer, ZyanUSize capacity)
{
    if (!source)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    const ZyanUSize len = source->size;

    if (capacity < len)
    {
        return ZYAN_STATUS_INSUFFICIENT_BUFFER_SIZE;
    }

    ZYAN_CHECK(ZyanVectorInitCustomBuffer(destination, source->element_size, buffer, capacity));
    ZYAN_ASSERT(destination->capacity >= len);

    ZYAN_MEMCPY(destination->data, source->data, len * source->element_size);
    destination->size = len;

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */
/* Element access                                                                                 */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorGetElement(const ZyanVector* vector, ZyanUSize index, const void** value)
{
    if (!vector || !value)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index >= vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    *value = (const void*)ZYCORE_VECTOR_OFFSET(vector, index);

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorGetElementMutable(const ZyanVector* vector, ZyanUSize index, void** value)
{
    if (!vector || !value)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index >= vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    *value = ZYCORE_VECTOR_OFFSET(vector, index);

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorSetElement(ZyanVector* vector, ZyanUSize index, const void* value)
{
    if (!vector || !value)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index >= vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    void* const offset = ZYCORE_VECTOR_OFFSET(vector, index);
    ZYAN_MEMCPY(offset, value, vector->element_size);

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */
/* Insertion                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorPush(ZyanVector* vector, const void* element)
{
    if (!vector || !element)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    if (ZYCORE_VECTOR_SHOULD_GROW(vector->size + 1, vector->capacity))
    {
        ZYAN_CHECK(ZyanVectorReallocate(vector,
            ZYAN_MAX(1, (ZyanUSize)((vector->size + 1) * vector->growth_factor))));
    }

    void* const offset = ZYCORE_VECTOR_OFFSET(vector, vector->size);
    ZYAN_MEMCPY(offset, element, vector->element_size);

    ++vector->size;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorInsert(ZyanVector* vector, ZyanUSize index, const void* element)
{
    return ZyanVectorInsertEx(vector, index, element, 1);
}

ZyanStatus ZyanVectorInsertEx(ZyanVector* vector, ZyanUSize index, const void* elements,
    ZyanUSize count)
{
    if (!vector || !elements || !count)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index > vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    if (ZYCORE_VECTOR_SHOULD_GROW(vector->size + count, vector->capacity))
    {
        ZYAN_CHECK(ZyanVectorReallocate(vector,
            ZYAN_MAX(1, (ZyanUSize)((vector->size + count) * vector->growth_factor))));
    }

    if (index < vector->size)
    {
        ZYAN_CHECK(ZyanVectorShiftRight(vector, index, count));
    }

    void* const offset = ZYCORE_VECTOR_OFFSET(vector, index);
    ZYAN_MEMCPY(offset, elements, count * vector->element_size);
    vector->size += count;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorEmplace(ZyanVector* vector, void** element, ZyanObjectFunction constructor)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    return ZyanVectorEmplaceEx(vector, vector->size, element, constructor);
}

ZyanStatus ZyanVectorEmplaceEx(ZyanVector* vector, ZyanUSize index, void** element,
    ZyanObjectFunction constructor)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index > vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    ZYAN_ASSERT(vector->element_size);
    ZYAN_ASSERT(vector->data);

    if (ZYCORE_VECTOR_SHOULD_GROW(vector->size + 1, vector->capacity))
    {
        ZYAN_CHECK(ZyanVectorReallocate(vector,
            ZYAN_MAX(1, (ZyanUSize)((vector->size + 1) * vector->growth_factor))));
    }

    if (index < vector->size)
    {
        ZYAN_CHECK(ZyanVectorShiftRight(vector, index, 1));
    }

    *element = ZYCORE_VECTOR_OFFSET(vector, index);
    if (constructor)
    {
        ZYAN_CHECK(constructor(*element));
    }

    ++vector->size;

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */
/* Deletion                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorDelete(ZyanVector* vector, ZyanUSize index)
{
    return ZyanVectorDeleteEx(vector, index, 1);
}

ZyanStatus ZyanVectorDeleteEx(ZyanVector* vector, ZyanUSize index, ZyanUSize count)
{
    if (!vector || !count)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (index + count >= vector->size)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    if (index < vector->size - 1)
    {
        ZYAN_CHECK(ZyanVectorShiftLeft(vector, index, count));
    }

    vector->size -= count;
    if (ZYCORE_VECTOR_SHOULD_SHRINK(vector->size, vector->capacity, vector->shrink_threshold))
    {
        return ZyanVectorReallocate(vector,
            ZYAN_MAX(1, (ZyanUSize)(vector->size * vector->growth_factor)));
    }

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorPop(ZyanVector* vector)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (vector->size == 0)
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    --vector->size;
    if (ZYCORE_VECTOR_SHOULD_SHRINK(vector->size, vector->capacity, vector->shrink_threshold))
    {
        return ZyanVectorReallocate(vector,
            ZYAN_MAX(1, (ZyanUSize)(vector->size * vector->growth_factor)));
    }

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorClear(ZyanVector* vector)
{
    return ZyanVectorResize(vector, 0);
}

/* ---------------------------------------------------------------------------------------------- */
/* Searching                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorFind(const ZyanVector* vector, const void* element, ZyanISize* found_index,
    ZyanEqualityComparison comparison)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    return ZyanVectorFindEx(vector, element, found_index, comparison, 0, vector->size);
}

ZyanStatus ZyanVectorFindEx(const ZyanVector* vector, const void* element, ZyanISize* found_index,
    ZyanEqualityComparison comparison, ZyanUSize index, ZyanUSize count)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if ((index + count > vector->size)|| (index == vector->size))
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }
    if (!count)
    {
        *found_index = -1;
        return ZYAN_STATUS_FALSE;
    }

    const void* left;
    for (ZyanUSize i = index; i < index + count; ++i)
    {
        ZYAN_CHECK(ZyanVectorGetElement(vector, i, &left));
        if (comparison(left, element))
        {
            *found_index = i;
            return ZYAN_STATUS_TRUE;
        }
    }

    *found_index = -1;
    return ZYAN_STATUS_FALSE;
}

ZyanStatus ZyanVectorBinarySearch(const ZyanVector* vector, const void* element,
    ZyanUSize* found_index, ZyanComparison comparison)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    return ZyanVectorBinarySearchEx(vector, element, found_index, comparison, 0, vector->size);
}

ZyanStatus ZyanVectorBinarySearchEx(const ZyanVector* vector, const void* element,
    ZyanUSize* found_index, ZyanComparison comparison, ZyanUSize index, ZyanUSize count)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (((index >= vector->size) && (count > 0)) || (index + count > vector->size))
    {
        return ZYAN_STATUS_OUT_OF_RANGE;
    }

    if (!count)
    {
        *found_index = index;
        return ZYAN_STATUS_FALSE;
    }

    const void* element_mid;

    ZyanStatus status = ZYAN_STATUS_FALSE;
    ZyanISize l = index;
    ZyanISize h = index + count - 1;
    while (l <= h)
    {
        const ZyanUSize mid = l + ((h - l) >> 1);
        ZYAN_CHECK(ZyanVectorGetElement(vector, mid, &element_mid));
        const ZyanI32 cmp = comparison(element_mid, element);
        if (cmp < 0)
        {
            l = mid + 1;
        } else
        {
            h = mid - 1;
            if (cmp == 0)
            {
                status = ZYAN_STATUS_TRUE;
            }
        }
    }

    *found_index = l;
    return status;
}

/* ---------------------------------------------------------------------------------------------- */
/* Memory management                                                                              */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorResize(ZyanVector* vector, ZyanUSize size)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    if (ZYCORE_VECTOR_SHOULD_GROW(size, vector->capacity) ||
        ZYCORE_VECTOR_SHOULD_SHRINK(size, vector->capacity, vector->shrink_threshold))
    {
        ZYAN_CHECK(ZyanVectorReallocate(vector, (ZyanUSize)(size * vector->growth_factor)));
    };

    vector->size = size;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorReserve(ZyanVector* vector, ZyanUSize capacity)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    if (capacity > vector->capacity)
    {
        ZYAN_CHECK(ZyanVectorReallocate(vector, capacity));
    }

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorShrinkToFit(ZyanVector* vector)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    return ZyanVectorReallocate(vector, vector->size);
}

/* ---------------------------------------------------------------------------------------------- */
/* Information                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

ZyanStatus ZyanVectorGetCapacity(const ZyanVector* vector, ZyanUSize* capacity)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    *capacity = vector->capacity;

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyanVectorGetSize(const ZyanVector* vector, ZyanUSize* size)
{
    if (!vector)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    *size = vector->size;

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
