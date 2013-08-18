#include <rleahylib/rleahylib.hpp>
#include <noise.hpp>
#include <random.hpp>
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <random>
#include <utility>
#include <cstring>
#include <atomic>


using namespace MCPP;


const Word width=1024;
const Word depth=256;
const Word height=1024;
const Word sample_freq=16;
const Word num_threads=4;
const SWord start_x=-512;
const SWord start_z=-512;
const Nullable<std::mt19937_64::result_type> seed=1878033377675837610ULL;
const String filename("../simplex");


void write_file (FILE * handle, const String & s) {

	auto c_string=s.ToCString();

	if (
		(fputs(
			static_cast<ASCIIChar *>(c_string),
			handle
		)<0) ||
		(fputc(
			'\0',
			handle
		)<0)
	) throw std::runtime_error("Error writing to file");

}


void write_file (FILE * handle, Byte c) {

	if (fputc(
		c,
		handle
	)<0) throw std::runtime_error("Error writing to file");

}


inline Double normalize (Double lo, Double hi, Double val) noexcept {

	if (lo==hi) {

		if (val==lo) return 0.5;
		if (val>lo) return 1;
		return 0;

	}

	bool complement;
	if (lo>hi) {

		complement=true;
		std::swap(lo,hi);

	} else {

		complement=false;

	}

	Double difference=val-lo;
	Double range=hi-lo;

	if (difference<0) return complement ? 1 : 0;
	if (difference>range) return complement ? 0 : 1;

	Double result=difference/range;

	return complement ? 1-result : result;

}


void execute (std::mt19937_64::result_type seed) {

	auto gen=std::mt19937_64();
	gen.seed(seed);
	StdOut << "Seed: " << seed << Newline;

	auto c_filename=Path::Combine(
		Path::GetPath(
			File::GetCurrentExecutableFileName()
		),
		filename+"_"+String(seed)+".txt"
	).ToCString();

	FILE * fhandle=fopen(
		static_cast<ASCIIChar *>(c_filename),
		"w"
	);

	if (fhandle==nullptr) {

		StdOut << "Could not open " << filename << " for writing" << Newline;

		return;

	}

	try {

		auto ocean=MakeOctave(
			Simplex(gen),
			4,
			0.7,
			0.00005
		);

		auto max=MakeScale(
			MakeOctave(
				Simplex(gen),
				4,
				0.9,
				0.00005
			),
			0,
			1
		);

		auto min=MakeScale(
			MakeBias(
				MakeOctave(
					Simplex(gen),
					4,
					0.5,
					0.00005
				),
				0.25
			),
			0,
			1
		);

		auto heightmap=MakeScale(
			MakeOctave(
				Simplex(gen),
				4,
				0.7,
				0.0005
			),
			0,
			1
		);

		auto rivers=MakeRidged(
			MakeOctave(
				Simplex(gen),
				4,
				0.8,
				0.0001
			)
		);

		auto perturbate_x=MakeScale(
			MakeOctave(
				MakeGain(
					Simplex(gen),
					0.25
				),
				3,
				0.4,
				0.01
			),
			-20,
			20
		);

		auto perturbate_z=MakeScale(
			MakeOctave(
				MakeGain(
					Simplex(gen),
					0.25
				),
				3,
				0.4,
				0.01
			),
			-20,
			20
		);

		auto noise=MakeConvert<Byte>(
			MakePerturbateDomain<1>(
				MakePerturbateDomain<4>(
					[&] (Double x, Double y, Double z) {
					
						//	Get primary noise values
						auto ocean_val=ocean(x,z);
						auto max_val=max(x,z);
						auto min_val=min(x,z);
						auto height_val=heightmap(x,z);
						
						//	Cap on the world's height
						Double ceiling;
						//	Floor on the world's height
						Double floor;
						//	Actual cap on the world's height
						//	used in terrain generation
						Double real_ceiling;
						//	Actual floor on the world's height
						//	used in terrain generation
						Double real_floor;
						//	The height of the solid world
						Double height;
						//	Result for terrain generation at this
						//	point
						Double result;
						
						//	Branch based on ocean/land
						if (ocean_val<-0.0125) {
						
							//	Ocean
							
							//	Determine ceiling and floor
							ceiling=fma(
								min_val,
								16,
								68
							);
							floor=fma(
								max_val,
								-16,
								48
							);
							if (floor>ceiling) std::swap(floor,ceiling);
							
							//	To enable a smooth transition between
							//	land and ocean we have a "continental
							//	shelf" where the land/beach drop away
							//	into the ocean
							//
							//	Apply this if necessary
							if (ocean_val<-0.025) {
							
								Double dampen=normalize(
									-0.025,
									-0.0125,
									ocean_val
								);
								real_floor=(dampen*(64-floor))+floor;
								real_ceiling=(dampen*(64-ceiling))+ceiling;
							
							} else {
							
								real_floor=floor;
								real_ceiling=ceiling;
							
							}
							
							//	Determine current height
							height=fma(
								height_val,
								real_ceiling-real_floor,
								real_floor
							);
							
							result=(y>height) ? ((y>64) ? 255 : 127) : 0;
						
						} else {
						
							//	Land
							
							//	Beaches are technically land, but
							//	are always at y=64
							//
							//	We calculate the floor/ceiling anyway
							//	so that it's available for river
							//	processing
							ceiling=(
								(max_val<0.3)
									?	72
									:	(
											(max_val<0.8)
												?	fma(
														normalize(
															0.3,
															0.8,
															max_val
														),
														256-72,
														72
													)
												:	256
										)
							);
							floor=(
								(min_val<0.2)
									?	fma(
											normalize(
												0,
												0.2,
												min_val
											),
											52-64,
											64
										)
									:	(
											(min_val<0.5)
												?	64
												:	(
														(min_val<0.9)
															?	fma(
																	normalize(
																		0.5,
																		0.9,
																		min_val
																	),
																	128-64,
																	64
																)
															:	128
													)
										)
							);
							if (floor>ceiling) std::swap(floor,ceiling);
							
							//	The land needs to fall off smoothly
							//	towards beaches, so apply this dampening
							//	if necessary
							if (
								(ocean_val>=0) &&
								(ocean_val<0.0125)
							) {
							
								Double dampen=normalize(
									0.0125,
									0,
									ocean_val
								);
								real_floor=(dampen*(64-floor))+floor;
								real_ceiling=(dampen*(64-ceiling))+ceiling;
							
							} else {
							
								real_floor=floor;
								real_ceiling=ceiling;
							
							}
							
							//	Determine current height
							height=(ocean_val<0) ? 64 : fma(
								height_val,
								real_ceiling-real_floor,
								real_floor
							);
							
							result=(y>height) ? 255 : 0;
						
						}
						
						//	Rivers
						
						//	Rivers do not occur in the ocean
						//	proper
						if (ocean_val>=-0.025) {
						
							//	This is the starting river threshold.
							//
							//	River noise higher than the final
							//	value causes a river or river-related
							//	terrain feature to form.
							Double river_threshold=0.965;
							Double river_threshold_start=river_threshold;
							//	This is the river transitions threshold.
							//	Within this distance from the river threshold
							//	transition terrain features will be formed
							Double river_transition_threshold=0.05;
							//	This is the river beach threshold.
							//	Within this distance from the river threshold
							//	beaches will be formed
							Double river_beach_threshold=0.0025;
							
							//	Now we adjust the threshold based
							//	on the surrounding terrain
							
							//	Use the average of the minimum,
							//	maximum, and height map to adjust
							//	the threshold
							Double avg=(max_val+min_val+height_val)/3;
							if (avg<0.5) river_threshold-=avg*2*0.06;
							else river_threshold+=(avg-0.5)*2*(1-river_threshold);
							
							//	Near oceans rivers widen out
							//	substantially
							if (ocean_val<0.04) river_threshold-=normalize(
								0.04,
								-0.025,
								ocean_val
							)*0.25;
							
							//	River noise value
							auto rivers_val=rivers(x,z);
							
							//	Test for a river
							if (rivers_val>river_threshold) {
							
								river_transition_threshold+=river_threshold;
								
								//	There needs to be a transition
								//	between regular terrain and
								//	a river, check which region we're
								//	in
								if (rivers_val<river_transition_threshold) {
								
									//	We're transitioning towards
									//	a river
									
									//	River transitions need not occur
									//	on beaches -- they're already at
									//	the proper height anyway -- or in
									//	the ocean -- it's already underwater
									//	anyway.
									if (ocean_val>=0) {
									
										river_beach_threshold=river_transition_threshold-river_beach_threshold;
										
										//	River transition or river beach?
										if (rivers_val<river_beach_threshold) {
										
											//	River transition
											
											//	"Pull" the world height towards
											//	y=64 to meet up with the river
											height=((height-64)*normalize(
												river_beach_threshold,
												river_threshold,
												rivers_val
											))+64;
										
										} else {
										
											//	River beach
											height=64;
										
										}
										
										//	Adjust result
										result=(y>height) ? 255 : 0;
									
									}
								
								} else {
								
									//	We're in a river
									
									//	Determine the river's depth
									
									//	This is the default maximum depth
									//	of a river
									Double river_depth=8;
									
									//	We use the threshold to adjust the depth
									if (river_threshold<river_threshold_start) {

										river_depth+=normalize(
											river_threshold_start,
											river_threshold_start-0.06-0.25,
											river_threshold
										)*16;

									} else {

										river_depth-=normalize(
											river_threshold_start,
											1,
											river_threshold
										)*8;

									}
									
									//	Determine how deep the river
									//	ought to be based on the strength
									//	of the noise
									Double river_noise_strength=normalize(
										river_transition_threshold,
										1,
										rivers_val
									);
									//	To obtain a nice curved river bottom
									//	we square this value.
									//
									//	See graph of y=x^2 for 0<=x<=1
									river_depth*=river_noise_strength*river_noise_strength;
									Double river_height=64-river_depth;
									
									height=(
										(ocean_val<-0.0125)
											?	river_height+(
													(height-river_height)*normalize(
														-0.0125,
														-0.025,
														ocean_val
													)
												)
											:	river_height
									);
									
									result=(y>64) ? 255 : ((y>height) ? 127 : 0);
								
								}
							
							}
						
						}
						
						return result;
					
					},
					perturbate_x
				),
				perturbate_z
			)
		);

		String width_str(width);
		String height_str(height);

		write_file(
			fhandle,
			String(seed)
		);
		write_file(
			fhandle,
			String(sample_freq)
		);
		write_file(
			fhandle,
			String(start_x)
		);
		write_file(
			fhandle,
			String(start_z)
		);
		write_file(
			fhandle,
			String(width_str)
		);
		write_file(
			fhandle,
			String(height_str)
		);

		Vector<Byte> buffer(width*height*depth);
		buffer.SetCount(width*height*depth);

		Timer timer(Timer::CreateAndStart());

		std::atomic<Word> blocks;
		blocks=0;
		std::atomic<Word> columns;
		columns=0;
		Vector<Thread> threads(num_threads);
		for (Word i=0;i<num_threads;++i) {

			threads.EmplaceBack(
				[&] (Word num) {

					Word lower=(width/num_threads)*num;
					Word upper=(
						(num==(num_threads-1))
							?	width
							:	(width/num_threads)*(num+1)
					);

					for (Word x=lower;x<upper;++x) {

						SWord real_x=static_cast<SWord>(x*sample_freq)+start_x;

						Word outer_offset=x*height*depth;

						for (Word z=0;z<height;++z) {

							SWord real_z=static_cast<SWord>(z*sample_freq)+start_z;
							Word offset=outer_offset+(z*depth);

							for (Word y=depth;(y--)>0;) {

								auto val=noise(real_x,y,real_z);

								buffer[offset+y]=val;

								++blocks;

								if (val==0) break;

							}

							++columns;

						}

					}

				},
				i
			);

		}

		Word backspaces=0;
		Word total_columns=height*width;
		while (columns!=total_columns) {

			Thread::Sleep(250);

			String output(
				String::Format(
					"Columns: {0}/{1} ({2}%)",
					Word(columns),
					total_columns,
					(static_cast<Double>(columns)/static_cast<Double>(total_columns))*100
				)
			);

			Word new_count=output.Size();

			String b;
			String s;
			for (Word i=0;i<backspaces;++i) {

				b << "\b";
				s << " ";

			}

			output=b+s+b+output;

			StdOut << output;

			backspaces=new_count;

		}

		String b;
		String s;
		for (Word i=0;i<backspaces;++i) {

			b << "\b";
			s << " ";

		}
		StdOut << b << s << b;

		for (auto & t : threads) t.Join();

		timer.Stop();

		StdOut	<< "====GENERATION COMPLETE===="
				<< Newline
				<< "Blocks placed: "
				<< static_cast<Word>(blocks)
				<< Newline
				<< "Elapsed time: "
				<< timer.ElapsedNanoseconds()
				<< "ns"
				<< Newline
				<< "Average time per block: "
				<< (timer.ElapsedNanoseconds()/blocks)
				<< "ns"
				<< Newline;

		for (Word x=0;x<width;++x)
		for (Word z=0;z<height;++z) {

			Word water_depth=0;

			for (Word y=depth-1;;--y) {

				if (y==0) {

					write_file(fhandle,0);

					break;

				}

				Byte val=buffer[(x*height*depth)+(z*depth)+y];

				if (val==0) {

					write_file(
						fhandle,
						(
							(water_depth==0)
								?	static_cast<Byte>(y)
								:	static_cast<Byte>(normalize(
										1,
										32,
										water_depth
									)*51)
						)
					);

					break;

				}

				if (val==127) ++water_depth;

			}

		}

	} catch (...) {

		fclose(fhandle);

		throw;

	}

	fclose(fhandle);

}


int main () {

	do {

		std::mt19937_64::result_type curr_seed=seed.IsNull() ? CryptoRandom<std::mt19937_64::result_type>() : *seed;

		execute(curr_seed);

	} while (seed.IsNull());

	return 0;

}