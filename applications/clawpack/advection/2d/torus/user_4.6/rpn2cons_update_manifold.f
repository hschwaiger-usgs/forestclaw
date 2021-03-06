      subroutine  rpn2_cons_update_manifold(meqn,maux, idir, q,
     &                                     auxvec_center,
     &                                     auxvec_edge,flux)

      implicit none

      integer meqn,maux,idir, m
      double precision q(meqn), flux(meqn)
      double precision auxvec_center(maux), auxvec_edge(maux)
      double precision u,v,w,urot, nv(3)
      integer k

c     # Cell-centered velocities         
      u = auxvec_center(4)
      v = auxvec_center(5)
      w = auxvec_center(6)

c     # x-face normal : (9-11)
c     # y-face normal : (12-14)       
      do k = 1,3
          if (idir .eq. 0) then
              nv(k) = auxvec_edge(9+k-1)
          else
              nv(k) = auxvec_edge(12+k-1)
          endif
      enddo

c     # Get flux in direction normal to idir-face
      urot = u*nv(1) + v*nv(2) + w*nv(3)

c     #  f(q) = (n dot u)*q
      do m = 1,meqn
c         # Don't multiply by edgelength (scaling is done elsewhere)
          flux(m) = urot*q(m)
      enddo

      end

      subroutine  rpn2_cons_update_zero(meqn,maux, idir, q,
     &                                  auxvec_center,
     &                                  auxvec_edge,flux)

      implicit none

      integer meqn,maux,idir, m
      double precision q(meqn), flux(meqn)
      double precision auxvec_center(maux), auxvec_edge(maux)

c     #  f(q) = (n dot u)*q
      do m = 1,meqn
c         # Don't multiply by edgelength (scaling is done elsewhere)
          flux(m) = 0
      enddo

      end

