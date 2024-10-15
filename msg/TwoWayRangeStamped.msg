std_msgs/Header header

# distance in [m] raw
float32 range_raw

# distance in [m] with bias corrections
float32 range_corr 

# measurement variance (standard deviation squared)
float32 R

# Unique source/master ID
uint32 UWB_ID1

# Unique destination/slave ID
uint32 UWB_ID2

# UWB source type: 'A' stationary anchor, 'T' moving tag, 'U' unknown
char type_ID1

# UWB destination type: 'A' stationary anchor, 'T' moving tag, 'U' unknown
char type_ID2

# Line of sight: estimate if inline or outlier
bool LOS
