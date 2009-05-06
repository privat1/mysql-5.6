/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef DATA_BUFFER2_HPP
#define DATA_BUFFER2_HPP

/**
 * @class  DataBuffer
 * @brief  Buffer of data words
 *
 * @note   The buffer is divided into segments (of size sz)
 */
template <Uint32 sz, typename Pool>
class DataBuffer2 {
public:
  struct Segment {
    Uint32 magic;
    Uint32 nextPool;
    Uint32 data[sz];
    NdbOut& print(NdbOut& out){
      out << "[DataBuffer<" << sz << ">::Segment this="
	  << this << dec << " nextPool= "
	  << nextPool << " ]";
      return out;
    }
  };
public:
  typedef Pool DataBufferPool;

  /**
   * Head/anchor for data buffer
   */
  struct Head {
    Head() ;

    Uint32 used;       // Words used
    Uint32 firstItem;  // First segment (or RNIL)
    Uint32 lastItem;   // Last segment (or RNIL)

    /**
     * Get size of databuffer, in words
     */
    Uint32 getSize() const { return used;}

    /**
     * Get segment size in words (template argument)
     */
    static Uint32 getSegmentSize() { return sz;}
  };

  /** Constructor */
  DataBuffer2(DataBufferPool &);

  /** Seize <b>n</b> words, Release */
  bool seize(Uint32 n);
  void release();

  /**
   * Get size of databuffer, in words
   */
  Uint32 getSize() const;

  /**
   * Check if buffer is empty
   */
  bool isEmpty() const;

  /**
   * Get segment size in words (template argument)
   */
  static Uint32 getSegmentSize();

  void print(FILE*) const;

  /* ----------------------------------------------------------------------- */

  struct DataBufferIterator {
    Ptr<Segment>       curr;  // Ptr to current segment
    Uint32*            data;  // Pointer to current data (word)
    Uint32             ind;   // Word index within a segment
    Uint32             pos;   // Absolute word position within DataBuffer

    void print(FILE* out) {
      fprintf(out, "[DataBufferIterator curr.i=%d, data=%p, ind=%d, pos=%d]\n",
	      curr.i, (void*) data, ind, pos);
    };

    inline bool isNull() const { return curr.isNull();}
    inline void setNull() { curr.setNull(); data = 0; ind = pos = RNIL;}
  };
  typedef DataBufferIterator Iterator;

  struct ConstDataBufferIterator {
    ConstPtr<Segment>  curr;
    const Uint32 *     data;
    Uint32             ind;
    Uint32             pos;

    inline bool isNull() const { return curr.isNull();}
    inline void setNull() { curr.setNull(); data = 0; ind = pos = RNIL;}
  };

  /**
   * Iterator
   * @parameter hops  Number of words to jump forward
   * @note DataBuffer::next returns false if applied to last word.
   */
  bool first(DataBufferIterator &);
  bool next(DataBufferIterator &);
  bool next(DataBufferIterator &, Uint32 hops);
  bool nextPool(DataBufferIterator &);

  /**
   * Set iterator to position
   */
  bool position(DataBufferIterator& it, Uint32 pos);

  /** Iterator */
  bool first(ConstDataBufferIterator &) const;
  bool next(ConstDataBufferIterator &) const;
  bool next(ConstDataBufferIterator &, Uint32 hops) const;
  bool nextPool(ConstDataBufferIterator &) const;

  /**
   * Returns true if it is possible to store <em>len</em>
   * no of words at position given in iterator.
   */
  bool importable(const DataBufferIterator, Uint32 len);

  /**
   * Stores <em>len</em> no of words starting at location <em>src</em> in
   * databuffer at position given in iterator.
   *
   * @return true if success, false otherwise.
   * @note Iterator is not advanced.
   */
  bool import(const DataBufferIterator &, const Uint32* src, Uint32 len);

  /**
   * Increases size with appends <em>len</em> words
   * @return true if success, false otherwise.
   */
  bool append(const Uint32* src, Uint32 len);

  static void createRecordInfo(Record_info & ri, Uint32 type_id);
protected:
  Head head;
  DataBufferPool &  thePool;

private:
  /**
   * This is NOT a public method, since the intension is that the import
   * method using iterators will be more effective in the future
   */
  bool import(Uint32 pos, const Uint32* src, Uint32 len);
};

template<Uint32 sz, typename Pool>
class LocalDataBuffer2 : public DataBuffer2<sz, Pool> {
public:
  LocalDataBuffer2(typename DataBuffer2<sz, Pool>::DataBufferPool & thePool,
                   typename DataBuffer2<sz, Pool>::Head & _src)
    : DataBuffer2<sz, Pool>(thePool), src(_src)
  {
    this->head = src;
  }

  ~LocalDataBuffer2(){
    src = this->head;
  }
private:
  typename DataBuffer2<sz, Pool>::Head & src;
};

template<Uint32 sz, typename Pool>
inline
DataBuffer2<sz, Pool>::Head::Head(){
  used = 0;
  firstItem = RNIL;
  lastItem = RNIL;
}

template<Uint32 sz, typename Pool>
inline
bool DataBuffer2<sz, Pool>::importable(const DataBufferIterator it, Uint32 len){
  return (it.pos + len < head.used);
}

template<Uint32 sz, typename Pool>
inline
bool DataBuffer2<sz, Pool>::position(DataBufferIterator& it, Uint32 p){

  // TODO: The current implementation is not the most effective one.
  //       A more effective implementation would start at the current
  //       position of the iterator.

  if(!first(it)){
    return false;
  }
  return next(it, p);
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::import(const DataBufferIterator & it,
                              const Uint32* src, Uint32 len)
{
  Uint32 ind = (it.pos % sz);
  Uint32 left = sz  - ind;
  Segment * p = it.curr.p;

  if (left)
  {
    memcpy(p->data+ind, src, 4 * left);
    if (len <= left)
      return true;

    src += left;
    len -= left;
  }

  while (len > sz)
  {
    p = static_cast<Segment*>(thePool.getPtr(p->nextPool));
    memcpy(p->data, src, 4 * sz);
    src += sz;
    len -= sz;
  }

  if (len)
  {
    p = static_cast<Segment*>(thePool.getPtr(p->nextPool));
    memcpy(p->data, src, 4 * len);
  }

  return true;
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::append(const Uint32* src, Uint32 len){
  if(len == 0)
    return true;

  Uint32 pos = head.used;
  if(!seize(len)){
    return false;
  }
  DataBufferIterator it;

  if(position(it, pos) && import(it, src, len)){
    return true;
  }
  abort();
  return false;
}

template<Uint32 sz, typename Pool>
inline
void DataBuffer2<sz, Pool>::print(FILE* out) const {
  fprintf(out, "[DataBuffer used=%d words, segmentsize=%d words",
	  head.used, sz);

  if (head.firstItem == RNIL) {
    fprintf(out, ": No segments seized.]\n");
    return;
  } else {
    fprintf(out, "\n");
  }

  Ptr<Segment> ptr;
  ptr.i = head.firstItem;

  Uint32 acc = 0;
  for(; ptr.i != RNIL; ){
    thePool.getPtr(ptr);
    const Uint32 * rest = ptr.p->data;
    for(Uint32 i = 0; i<sz; i++){
      fprintf(out, " H'%.8x", rest[i]);
      if(acc++ == 6){
	acc = 0;
	fprintf(out, "\n");
      }
    }
    ptr.i = ptr.p->nextPool;
  }
  fprintf(out, " ]\n");
}

template<Uint32 sz, typename Pool>
inline
DataBuffer2<sz, Pool>::DataBuffer2(DataBufferPool & p) : thePool(p){
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::seize(Uint32 n){
  Uint32 rest; // Free space in last segment (currently)

  if(head.firstItem == RNIL)
  {
    rest = 0;
  }
  else
  {
    rest = (sz - (head.used % sz)) % sz;
  }

  if (0)
    ndbout_c("seize(%u) used: %u rest: %u firstItem: 0x%x",
             n, head.used, rest, head.firstItem);

  if (rest >= n)
  {
    head.used += n;
    return true;
  }

  Uint32 used = head.used + n;
  Segment first;
  Ptr<Segment> currPtr;
  currPtr.p = &first;
  first.nextPool = RNIL;

  while (n >= sz)
  {
    Ptr<void> tmp;
    if (thePool.seize(tmp))
    {
      currPtr.p->nextPool = tmp.i;
      currPtr.i = tmp.i;
      currPtr.p = static_cast<Segment*>(tmp.p);
    }
    else
    {
      goto error;
    }
    n -= sz;
  }

  if(n > rest)
  {
    Ptr<void> tmp;
    if (thePool.seize(tmp))
    {
      currPtr.p->nextPool = tmp.i;
      currPtr.i = tmp.i;
      currPtr.p = static_cast<Segment*>(tmp.p);
    }
    else
    {
      goto error;
    }
  }

  if (head.firstItem == RNIL)
  {
    head.firstItem = first.nextPool;
  }
  else
  {
    Segment* lastPtr = static_cast<Segment*>(thePool.getPtr(head.lastItem));
    lastPtr->nextPool = first.nextPool;
  }

  head.used = used;
  head.lastItem = currPtr.i;
  currPtr.p->nextPool = RNIL;
  return true;

error:
  currPtr.i = first.nextPool;
  while (currPtr.i != RNIL)
  {
    currPtr.p = static_cast<Segment*>(thePool.getPtr(currPtr.i));
    Ptr<void> tmp;
    tmp.i = currPtr.i;
    tmp.p = currPtr.p;
    currPtr.i = currPtr.p->nextPool;
    thePool.release(tmp);
  }
  return false;
}

template<Uint32 sz, typename Pool>
inline
void
DataBuffer2<sz, Pool>::release(){
  Ptr<void> tmp;
  tmp.i = head.firstItem;
  while (tmp.i != RNIL)
  {
    tmp.p = thePool.getPtr(tmp.i);
    Uint32 next = static_cast<Segment*>(tmp.p)->nextPool;
    thePool.release(tmp);
    tmp.i = next;
  }
}

template<Uint32 sz, typename Pool>
inline
Uint32
DataBuffer2<sz, Pool>::getSegmentSize(){
  return sz;
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::first(DataBufferIterator & it){
  return first((ConstDataBufferIterator&)it);
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::next(DataBufferIterator & it){
  return next((ConstDataBufferIterator&)it);
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::next(DataBufferIterator & it, Uint32 hops){
  return next((ConstDataBufferIterator&)it, hops);
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::first(ConstDataBufferIterator & it) const {
  it.curr.i = head.firstItem;
  if(it.curr.i == RNIL){
    it.setNull();
    return false;
  }
  it.curr.p = static_cast<Segment*>(thePool.getPtr(it.curr.i));
  it.data = &it.curr.p->data[0];
  it.ind = 0;
  it.pos = 0;
  return true;
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::next(ConstDataBufferIterator & it) const {
  it.ind ++;
  it.data ++;
  it.pos ++;
  if(it.ind < sz && it.pos < head.used){
    return true;
  }

  if(it.pos < head.used){
    it.curr.i = it.curr.p->nextPool;
#ifdef ARRAY_GUARD
    if(it.curr.i == RNIL){
      /**
       * This is actually "internal error"
       * pos can't be less than head.used and at the same time we can't
       * find next segment
       *
       * Note this must not "really" be checked since thePool.getPtr will
       *  abort when trying to get RNIL. That's why the check is within
       *  ARRAY_GUARD
       */
      ErrorReporter::handleAssert("DataBuffer2<sz, Pool>::next", __FILE__, __LINE__);
    }
#endif
    it.curr.p = static_cast<Segment*>(thePool.getPtr(it.curr.i));
    it.data = &it.curr.p->data[0];
    it.ind = 0;
    return true;
  }
  it.setNull();
  return false;
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::next(ConstDataBufferIterator & it, Uint32 hops) const {
#if 0
  for (Uint32 i=0; i<hops; i++) {
    if (!this->next(it))
      return false;
  }
  return true;
#else
  if(it.pos + hops < head.used){
    while(hops >= sz){
      it.curr.i = it.curr.p->nextPool;
      it.curr.p = static_cast<Segment*>(thePool.getPtr(it.curr.i));
      hops -= sz;
      it.pos += sz;
    }

    it.ind += hops;
    it.pos += hops;
    if(it.ind < sz){
      it.data = &it.curr.p->data[it.ind];
      return true;
    }

    it.curr.i = it.curr.p->nextPool;
    it.curr.p = static_cast<Segment*>(thePool.getPtr(it.curr.i));
    it.ind -= sz;
    it.data = &it.curr.p->data[it.ind];
    return true;
  }
  it.setNull();
  return false;
#endif
}

template<Uint32 sz, typename Pool>
inline
Uint32
DataBuffer2<sz, Pool>::getSize() const {
  return head.used;
}

template<Uint32 sz, typename Pool>
inline
bool
DataBuffer2<sz, Pool>::isEmpty() const {
  return (head.used == 0);
}

template<Uint32 sz, typename Pool>
inline
void
DataBuffer2<sz, Pool>::createRecordInfo(Record_info & ri, Uint32 type_id)
{
  Segment tmp;
  const char * off_base = (char*)&tmp;
  const char * off_next = (char*)&tmp.nextPool;
  const char * off_magic = (char*)&tmp.magic;

  ri.m_size = sizeof(tmp);
  ri.m_offset_next_pool = Uint32(off_next - off_base);
  ri.m_offset_magic = Uint32(off_magic - off_base);
  ri.m_type_id = type_id;
}

#endif

