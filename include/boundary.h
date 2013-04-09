#ifndef BOUNDARY
#define BOUNDARY

#include "grid.h"
#include "fields.h"
#include "mpiinterface.h"

struct field3dbc
{
  double bot;
  double top;
  int bcbot;
  int bctop;
};

class cboundary
{
  public:
    cboundary(cgrid *, cfields *, cmpi *);
    ~cboundary();

    int readinifile(cinput *);
    int init();
    int setvalues();
    int exec();

    // TODO surface pointers
    double *obuk;
    double *ustar;

  private:
    cgrid   *grid;
    cfields *fields;
    cmpi    *mpi;

    int setbc      (double *, double *, double *, int, double, double);
    int setbc_patch(double *, double, double, double);

    // int setgcbot_2nd(double *, double *, int, double);
    // int setgctop_2nd(double *, double *, int, double);
    // int setgcbot_4th(double *, double *, int, double);
    // int setgctop_4th(double *, double *, int, double);

    int setgcbot_2nd(double *, double *, int, double *, double *);
    int setgctop_2nd(double *, double *, int, double *, double *);
    int setgcbot_4th(double *, double *, int, double *, double *);
    int setgctop_4th(double *, double *, int, double *, double *);

    int setgcbotw_4th(double *);
    int setgctopw_4th(double *);

    std::string swboundary;
    std::string swboundarytype;

    int mbcbot;
    int mbctop;

    typedef std::map<std::string, field3dbc *> bcmap;
    bcmap sbc;

    // patch type
    int    patch_dim;
    double patch_xh;
    double patch_xr;
    double patch_xi;
    double patch_facr;
    double patch_facl;

    inline double grad4x(const double, const double, const double, const double);

    //todo move this later
    // surface scheme
    int surface();
    int stability(double *, double *, double *,
                  double *, double *, double *,
                  double *, double *, double *,
                  double *, double *, double *);
    int surfm(double *, double *, double *,
              double *, double *, double *,
              double, int);
    int surfs(double *, double *, double *,
              double *, double *, double *,
              double, int);
    double calcobuk(double, double, double);
    inline double fm(double, double, double);
    inline double fh(double, double, double);
    inline double psim(double);
    inline double psih(double);
    inline double phim(double);
    inline double phih(double);
    double ustarin;
    double z0m;
    double z0h;

    typedef std::map<std::string, int> bcbotmap;
    int surfmbcbot;
    bcbotmap surfsbcbot;
};
#endif
