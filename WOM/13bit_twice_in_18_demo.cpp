// Demo program illustrating how to write 13 bit twice in a 18 bit WOM (Rfixed=1.4444).
// It uses "coset coding", with a predefined parity check matrix.
// The coding principle was derived from "Efficient Two-Write WOM-Codes" (Yaakobi et al) 
// Matrix manipulation uses bit parallel operations to increase speed
// Comments to bert.o.dobbelaere[at]telenet[dot]be

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#define N 18 // Total nr of pattern bits
#define K 5 // Max bits set in 1st write pattern
#define NSYMBOLS (1<<(N-K))
// 2**11 hash buckets of size 6, that's an over-provisioning of a factor 3/2 to store the 2**13 patterns for the 1st write decoding function
// In this example, it provides a reasonable balance between memory requirement and speed.
#define NBUCKETS (1<<11)
#define BUCKETSIZE 6
#define EMPTYPOS 0xFFFFFFFFu

typedef unsigned u32;

// Parity check matrix 18x13 bit
const u32 a[N]={0x13DE,0x051B,0x0106,0x0A9E,0x0154,0x0379,0x1C64,0x1667,0x19BF,0x0F53,0x0AC1,0x0414,0x10C9,0x1EC9,0x008F,0x1EA1,0x13E6,0x0B3B};


u32 symb_to_p[NSYMBOLS];           // Maps symbol to 1st write pattern
u32 symb_to_fixedbits[NSYMBOLS];   // Maps symbol to 5 bits that will remain fixed during 2nd write
u32 pattern_buckets[NBUCKETS][BUCKETSIZE]; // Hash buckets for 1st decode
u32 offset_pos[NBUCKETS]; // Symbol numbering start for each bucket
std::vector<u32> plist; // Only used during table init, list of 'potential' 1st write patterns. 



// Handles binary matrices with max 32 columns, nrows>=ncolumns
// 1 row per input word. Input matrix is not copied, so modified!
bool hasfullrank(u32 a[], u32 nrows, u32 ncols)
{
	u32 i=0;
	for(u32 diagbit=1ULL<<(ncols-1) ; diagbit!=0 ; diagbit>>=1)
	{
		for(u32 k=i+1;k<nrows;k++)
		{
			if(a[i]&diagbit)
				break;
			a[i]^=a[k];
		}
		if((a[i]&diagbit)==0)
			return false; //no pivot found
		
		for(u32 k=i+1;k<nrows;k++)
			if(a[k]&diagbit)
				a[k]^=a[i];
		i++;
	}
	return true;
}

// Recursively called routine, finding full rank minors of the parity check matrix
u32 countinv(const u32 a[], u32 rows, u32 lowestremoved, u32 pattern)
{
	u32 count=0;
	u32 m[rows];
	
	memcpy(m,a,rows*sizeof(u32));
	if(hasfullrank(m,rows,N-K))
	{
		count++;
		plist.push_back(pattern);
		if(rows > (N-K))
		{
			for(u32 r=0;r<lowestremoved;r++)
			{
				u32 idx=0;
				for(u32 i=0;i<r;i++)
					m[idx++]=a[i];
				for(u32 i=r+1;i<rows;i++)
					m[idx++]=a[i];
				count+=countinv(m,rows-1,r, pattern|(1<<r));
			}
		}	
	}	
	return count;
}

// Count number of bits set in a pattern
u32 bitcount(u32 p)
{
	u32 count=0;
	while(p)
	{
		count+=p&1;
		p>>=1;
	}
	return count;
}

// Computes syndrome using the parity check matrix
u32 compute_syndrome(u32 pattern)
{
	u32 syndrome=0;
	for(u32 n=0;n<N;n++,pattern>>=1)
		if(pattern&1)
			syndrome^=a[n];
	return syndrome;
}

// Maps 18 bit pattern to 11 bit hash
u32 hashfunc(u32 pattern)
{   
	return compute_syndrome(pattern)%NBUCKETS;
}

// One time initialisation of memory structures. May be replaced by hardcoded tables.
void init_tables()
{
	memset(pattern_buckets,0xFF,sizeof(pattern_buckets));
	memset(offset_pos,0,sizeof(offset_pos));
	
	printf("Initializing tables.\n");

	countinv(a,N,N,0);

	printf("Available 1st write vectors = %lu\n",plist.size());
	
	// As hash function to decode the first write symbol, we use a truncated version of the syndrom computation
	// (conveniently available). Collisions are resolved by storing at most BUCKETSIZE entries in a bucket.
	// A small amount of overflowing buckets can be tolerated as the unrestricted code supports 8380-8192=188
	// excess first write symbols.  
	
	u32 nsymb=0;
	for(u32 n=0;n<plist.size();n++)
	{
		u32 p=plist[n];
		
		u32 hash=hashfunc(p);
		u32 b;
		for(b=0;b<BUCKETSIZE;b++)
			if(pattern_buckets[hash][b]==EMPTYPOS)
				break;
		if(b<BUCKETSIZE)
		{
			if(nsymb<NSYMBOLS) // Restrict to fixed rate 
			{
				pattern_buckets[hash][b]=p;
				nsymb++;
			}
		}
	}
	assert(nsymb==NSYMBOLS); // 8K symbols should be mapped in the hash
	printf("Mapped 1st write vectors = %u\n",nsymb);
	

	u32 currentoffset=0;
	for(u32 n=0;n<NBUCKETS;n++)
	{
		offset_pos[n]=currentoffset;
		for(u32 b=0;b<BUCKETSIZE;b++)
		{
			u32 p=pattern_buckets[n][b];

			if(p!=EMPTYPOS) 
			{
				symb_to_p[currentoffset]= p;
				symb_to_fixedbits[currentoffset]=p; // Between 0 and 5 bits initially, will be adjusted to 5 below
				currentoffset++;
			}
		}
	}
	
	for(u32 s=0;s<nsymb;s++)
		if(bitcount(symb_to_fixedbits[s])<K)
		{
			u32 t;
			for(t=0;t<nsymb;t++)
			{
				if(((symb_to_fixedbits[t]&symb_to_fixedbits[s])==symb_to_fixedbits[s]) && (bitcount(symb_to_fixedbits[t])==K))
				{
					symb_to_fixedbits[s]=symb_to_fixedbits[t];
					break;
				}
			}
			assert(t<nsymb); // For each 1st write pattern, an 'extended' pattern with 5 bits set is to be found
			// So for encoding the 2nd symbol we only need to work with square matrices
		}		
}

// Encode the 1st write symbol into a pattern
u32 encode1(u32 s1)
{
	return symb_to_p[s1];
}

// Decode the 2nd write symbol from a pattern
u32 decode2(u32 pattern)
{
	return compute_syndrome(pattern);
}

// Decod the 1st write symbol from a pattern
u32 decode1(u32 pattern)
{
	u32 hash=hashfunc(pattern);
	u32 symb=offset_pos[hash];
	for(u32 b=0;b<BUCKETSIZE;b++)
	{
		u32 p=pattern_buckets[hash][b];
		if(p==pattern)
			return symb;
		symb+=1;
	}
	return EMPTYPOS;
}

// Solve linear system over GF(2)
// Gauss-Jordan elimination of enhanced matrix, rows are bit-packed.
void solvesystem(u32 A[]) 
{
	u32 nrows=N-K; // ncolumns = nrows+1
	u32 i=0;
	for(u32 diagbit=1 ; diagbit<(1u<<nrows) ; diagbit<<=1)
	{
		for(u32 k=i+1;k<nrows;k++)
		{
			if(A[i]&diagbit)
				break;
			A[i]^=A[k];
		}
		assert((A[i]&diagbit)!=0); // pivot must be found, otherwise not invertible
		
		for(u32 k=i+1;k<nrows;k++)
			if(A[k]&diagbit)
			{
				A[k]^=A[i];
			}
		i++;
	}
	
	i=0;
	for(u32 diagbit=1 ; diagbit<(1u<<nrows) ; diagbit<<=1)
	{	
		for(u32 k=0;k<i;k++)
			if(A[k]&diagbit)
			{
				A[k]^=A[i];
			}
		i++;
	}
	
	// A is now unity matrix enhanced with solution column (bit position N-K)
}


// Encodes the 2nd write symbol in a pattern, knowing the pattern after 1st write
u32 encode2(u32 s2, u32 pattern1)
{
	u32 s1=decode1(pattern1);
	u32 fixedbits=symb_to_fixedbits[s1]; // exactly 5 bits, superset of pattern1 bits
	
	u32 target = s2 ^ decode2(pattern1);
	
	u32 m[N-K]={0};
	u32 rows=0;
	for(u32 b=0;b<N;b++)
	{
		if(((fixedbits>>b)&1)==0)
		{
			for(u32 i=0;i<(N-K);i++) // Add coefficient column
				m[i] |=  ((a[b]>>i)&1) << rows;
			rows++;
		}
	}
	
	for(u32 i=0;i<(N-K);i++) // Add enhanced column
		m[i] |= ((target>>i)&1) << rows;

	solvesystem(m);
	
	u32 pattern=pattern1;
	
	rows=0;
	for(u32 b=0;b<N;b++)
	{
		if(((fixedbits>>b)&1)==0)
		{
			pattern |= ((m[rows]>>(N-K))&1)<<b;
			rows++;
		}
	}
	return pattern;
}



int main()
{
	printf("\nThis example illustrates the encoding/decoding of a <(2^13),(2^13)>/18  WOM code.\n");
	printf("Assumption is that encoder and decoder know the write number.\n");
	init_tables();
	
	// Test the encoding/decoding process
	printf("Testing our code by encoding/decoding random 13 bit values (hex):\n\n");
	printf("Encode 1        Pattern 1       Decode 1          Encode 2           Pattern 2       Decode 2\n\n");
	
	for(u32 n=0;n<20;n++)
	{
		u32 s1_enc= rand()%NSYMBOLS;
		u32 s2_enc= rand()%NSYMBOLS;
	
		u32 pattern1=encode1(s1_enc);
		
		printf("e1(%04X)   =>   p1=%05X   ",s1_enc,pattern1);
		
		if(pattern1 >= (1<<N))
		{
			printf("\nERROR: 1st pattern invalid.\n");
			assert(0);
		}
		
		u32 s1_dec = decode1(pattern1);
		printf("=>   d1(p1)=%04X       ",s1_dec);
	
		if(s1_dec!=s1_enc)
		{
			printf("ERROR: 1st symbol decode failed !\n");
			assert(0);
		}
	
		u32 pattern2=encode2(s2_enc,pattern1);
		
		printf("e2(%04X,p1)   =>   p2=%05X",s2_enc,pattern2);
		
		if((pattern2 >= (1<<N)) || ((pattern2&pattern1)!=pattern1) /* Check against "cheating" bits */)
		{
			printf("ERROR: 2nd pattern invalid.\n");
			assert(0);
		}
	
		u32 s2_dec = decode2(pattern2);
		printf("   =>   d2(p2)=%04X\n",s2_dec);
		if(s2_dec!=s2_enc)
		{
			printf("ERROR: 2nd symbol decode failed ! (Expected %04X, decoded %04X)\n",s2_enc, s2_dec);
			assert(0);
		}
	}
}




