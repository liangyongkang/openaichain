#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/seq/seq.hpp>
#include <boost/preprocessor/stringize.hpp>

#define OACLIB_REFLECT_MEMBER_OP( r, OP, elem ) \
  OP t.elem 

/**
 *  @def OACLIB_SERIALIZE(TYPE,MEMBERS)
 *
 *  @brief Specializes oac::reflector for TYPE where
 *         type inherits other reflected classes
 *
 *  @param INHERITS - a sequence of base class names (basea)(baseb)(basec)
 *  @param MEMBERS - a sequence of member names.  (field1)(field2)(field3)
 */
#define OACLIB_SERIALIZE( TYPE,  MEMBERS ) \
 template<typename DataStream> \
 friend DataStream& operator << ( DataStream& ds, const TYPE& t ){ \
    return ds BOOST_PP_SEQ_FOR_EACH( OACLIB_REFLECT_MEMBER_OP, <<, MEMBERS );\
 }\
 template<typename DataStream> \
 friend DataStream& operator >> ( DataStream& ds, TYPE& t ){ \
    return ds BOOST_PP_SEQ_FOR_EACH( OACLIB_REFLECT_MEMBER_OP, >>, MEMBERS );\
 } 


#define OACLIB_SERIALIZE_DERIVED( TYPE, BASE, MEMBERS ) \
 template<typename DataStream> \
 friend DataStream& operator << ( DataStream& ds, const TYPE& t ){ \
    ds << static_cast<const BASE&>(t); \
    return ds BOOST_PP_SEQ_FOR_EACH( OACLIB_REFLECT_MEMBER_OP, <<, MEMBERS );\
 }\
 template<typename DataStream> \
 friend DataStream& operator >> ( DataStream& ds, TYPE& t ){ \
    ds >> static_cast<BASE&>(t); \
    return ds BOOST_PP_SEQ_FOR_EACH( OACLIB_REFLECT_MEMBER_OP, >>, MEMBERS );\
 } 
