# TopicMessage Specification for DB_SAM Viewer Test
# Format: field_name	type	length	decimal	is_key
magic	X	4	0	Y
topic	9	4	0	N
global_seq	9	4	0	N
topic_seq	9	4	0	N
timestamp	9	8	0	N
data_size	9	4	0	N
data	X	32	0	N